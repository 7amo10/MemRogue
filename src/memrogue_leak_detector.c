/**
 * @file memrogue_leak_detector.c
 * @brief Leak detection engine implementation.
 *
 * This module implements leak detection by scanning the tracker's hash table,
 * identifying unfreed allocations, and grouping them by backtrace signature.
 *
 * Thread Safety:
 * - leak_detector_scan() is thread-safe, takes snapshot under lock
 * - All internal operations use local state only (no global mutable state)
 * - Returned report is fully owned by caller
 * - Create/clone functions are thread-safe (return new objects)
 * - Modification functions (add/remove/merge) are NOT thread-safe
 *
 * MEMRO-14: Leak Detection Engine
 * MEMRO-18: Leak Report Structure
 */

#include "memrogue_leak_detector.h"
#include "memrogue_tracker.h"
#include "memrogue_allocation_record.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

/** Severity thresholds in bytes */
#define SEVERITY_LOW_THRESHOLD      (1024UL)           /* 1 KB */
#define SEVERITY_MEDIUM_THRESHOLD   (1024UL * 1024)    /* 1 MB */
#define SEVERITY_HIGH_THRESHOLD     (100UL * 1024 * 1024)  /* 100 MB */

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * Context for leak collection callback.
 * All fields are owned by the scan operation (stack-allocated).
 */
typedef struct {
    leak_group_t* groups;               /**< Head of groups linked list */
    size_t group_count;                 /**< Number of groups */
    size_t total_leaks;                 /**< Total leak count */
    size_t total_bytes;                 /**< Total bytes leaked */
    const leak_detector_config_t* config;  /**< Detection configuration */
    bool allocation_failed;             /**< Flag set on allocation failure */
} leak_collection_context_t;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Get current time in microseconds for timing measurements.
 */
static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

/**
 * Compute severity level based on total leaked bytes.
 */
static leak_severity_t compute_severity(size_t total_bytes) {
    if (total_bytes == 0) {
        return LEAK_SEVERITY_NONE;
    } else if (total_bytes < SEVERITY_LOW_THRESHOLD) {
        return LEAK_SEVERITY_LOW;
    } else if (total_bytes < SEVERITY_MEDIUM_THRESHOLD) {
        return LEAK_SEVERITY_MEDIUM;
    } else if (total_bytes < SEVERITY_HIGH_THRESHOLD) {
        return LEAK_SEVERITY_HIGH;
    } else {
        return LEAK_SEVERITY_CRITICAL;
    }
}

/**
 * Duplicate a string (internal helper).
 * Returns NULL if input is NULL or allocation fails.
 */
static char* string_dup(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    if (copy != NULL) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

/**
 * Create a new leak entry from allocation info (internal use during scan).
 * Does NOT copy strings - points to tracker's copy.
 * Returns NULL on allocation failure.
 */
static leak_entry_t* internal_leak_entry_create(const allocation_info_t* info) {
    if (info == NULL) {
        return NULL;
    }
    
    leak_entry_t* entry = calloc(1, sizeof(leak_entry_t));
    if (entry == NULL) {
        return NULL;
    }
    
    entry->address = info->ptr;
    entry->size = info->size;
    entry->file = info->file;  /* Point to tracker's copy - valid during scan */
    entry->line = info->line;
    entry->timestamp = info->timestamp;
    entry->frame_count = info->frame_count;
    entry->next = NULL;
    
    /* Copy backtrace frames */
    if (info->frame_count > 0) {
        memcpy(entry->frames, info->frames, 
               (size_t)info->frame_count * sizeof(void*));
    }
    
    return entry;
}

/**
 * Destroy entry chain (internal - does not free owned strings).
 */
static void internal_leak_entry_destroy_chain(leak_entry_t* entry) {
    while (entry != NULL) {
        leak_entry_t* next = entry->next;
        free(entry);
        entry = next;
    }
}

/**
 * Create a new leak group from allocation info (internal use during scan).
 * Does NOT copy strings.
 * Returns NULL on allocation failure.
 */
static leak_group_t* internal_leak_group_create(uint64_t signature, 
                                                 const allocation_info_t* first_info) {
    if (first_info == NULL) {
        return NULL;
    }
    
    leak_group_t* group = calloc(1, sizeof(leak_group_t));
    if (group == NULL) {
        return NULL;
    }
    
    group->signature = signature;
    group->frame_count = first_info->frame_count;
    group->file = first_info->file;  /* Point to tracker's copy */
    group->line = first_info->line;
    group->leak_count = 0;
    group->total_bytes = 0;
    group->entries = NULL;
    group->next = NULL;
    
    /* Copy representative backtrace */
    if (first_info->frame_count > 0) {
        memcpy(group->frames, first_info->frames,
               (size_t)first_info->frame_count * sizeof(void*));
    }
    
    return group;
}

/**
 * Destroy a leak group (internal - does not free owned strings).
 */
static void internal_leak_group_destroy(leak_group_t* group) {
    if (group == NULL) {
        return;
    }
    internal_leak_entry_destroy_chain(group->entries);
    free(group);
}

/**
 * Destroy a linked list of leak groups (internal).
 */
static void internal_leak_group_destroy_chain(leak_group_t* group) {
    while (group != NULL) {
        leak_group_t* next = group->next;
        internal_leak_group_destroy(group);
        group = next;
    }
}

/**
 * Find or create a group matching the given backtrace signature.
 * Returns NULL on allocation failure.
 */
static leak_group_t* find_or_create_group(leak_collection_context_t* ctx,
                                           const allocation_info_t* info) {
    uint64_t signature = backtrace_compute_signature(
        (void* const*)info->frames, info->frame_count);
    
    /* Search existing groups */
    leak_group_t* group = ctx->groups;
    while (group != NULL) {
        if (group->signature == signature &&
            backtrace_equals((void* const*)group->frames, group->frame_count,
                            (void* const*)info->frames, info->frame_count)) {
            return group;
        }
        group = group->next;
    }
    
    /* Check max groups limit */
    if (ctx->config->max_groups > 0 && ctx->group_count >= ctx->config->max_groups) {
        /* Return the first group as a catch-all */
        return ctx->groups;
    }
    
    /* Create new group */
    leak_group_t* new_group = internal_leak_group_create(signature, info);
    if (new_group == NULL) {
        ctx->allocation_failed = true;
        return NULL;
    }
    
    /* Insert at head of list */
    new_group->next = ctx->groups;
    ctx->groups = new_group;
    ctx->group_count++;
    
    return new_group;
}

/**
 * Add an entry to a group.
 * Returns true on success, false on allocation failure.
 */
static bool add_entry_to_group(leak_group_t* group, const allocation_info_t* info,
                                const leak_detector_config_t* config) {
    /* Update group statistics */
    group->leak_count++;
    group->total_bytes += info->size;
    
    /* Check if we should add the entry details */
    if (config->max_entries_per_group > 0) {
        /* Count current entries */
        size_t entry_count = 0;
        leak_entry_t* e = group->entries;
        while (e != NULL) {
            entry_count++;
            e = e->next;
        }
        
        if (entry_count >= config->max_entries_per_group) {
            return true;  /* Statistics updated, but don't add entry */
        }
    }
    
    /* Create and add entry */
    leak_entry_t* entry = internal_leak_entry_create(info);
    if (entry == NULL) {
        return false;
    }
    
    /* Insert at head of entries list */
    entry->next = group->entries;
    group->entries = entry;
    
    return true;
}

/**
 * Callback function for tracker iteration.
 * Collects leak information into the context.
 */
static bool collect_leak_callback(const allocation_info_t* info, void* user_data) {
    leak_collection_context_t* ctx = (leak_collection_context_t*)user_data;
    
    /* Skip if already had allocation failure */
    if (ctx->allocation_failed) {
        return false;  /* Stop iteration */
    }
    
    /* Apply minimum size filter */
    if (info->size < ctx->config->min_leak_size) {
        return true;  /* Continue iteration */
    }
    
    /* Update totals */
    ctx->total_leaks++;
    ctx->total_bytes += info->size;
    
    /* Find or create group for this leak */
    leak_group_t* group;
    if (ctx->config->group_by_backtrace && info->frame_count > 0) {
        group = find_or_create_group(ctx, info);
    } else {
        /* No grouping - create a unique group for each leak */
        group = internal_leak_group_create(ctx->total_leaks, info);
        if (group != NULL) {
            group->next = ctx->groups;
            ctx->groups = group;
            ctx->group_count++;
        }
    }
    
    if (group == NULL) {
        ctx->allocation_failed = true;
        return false;  /* Stop iteration */
    }
    
    /* Add entry to group */
    if (!add_entry_to_group(group, info, ctx->config)) {
        ctx->allocation_failed = true;
        return false;  /* Stop iteration */
    }
    
    return true;  /* Continue iteration */
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

void leak_detector_config_init(leak_detector_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    config->group_by_backtrace = true;
    config->include_backtraces = true;
    config->max_groups = 0;  /* Unlimited */
    config->max_entries_per_group = 10;
    config->min_leak_size = 0;
}

leak_report_t* leak_detector_scan(memory_tracker_t* tracker,
                                   const leak_detector_config_t* config) {
    if (tracker == NULL) {
        return NULL;
    }
    
    uint64_t start_time = get_time_us();
    
    /* Use default config if none provided */
    leak_detector_config_t default_config;
    if (config == NULL) {
        leak_detector_config_init(&default_config);
        config = &default_config;
    }
    
    /* Allocate report */
    leak_report_t* report = calloc(1, sizeof(leak_report_t));
    if (report == NULL) {
        return NULL;
    }
    
    /* Initialize collection context (stack-allocated, no race conditions) */
    leak_collection_context_t ctx = {
        .groups = NULL,
        .group_count = 0,
        .total_leaks = 0,
        .total_bytes = 0,
        .config = config,
        .allocation_failed = false
    };
    
    /* Iterate over all tracked allocations
     * Note: tracker_iterate handles its own locking internally */
    tracker_iterate(tracker, collect_leak_callback, &ctx);
    
    /* Check for allocation failure during iteration */
    if (ctx.allocation_failed) {
        /* Clean up partial results */
        internal_leak_group_destroy_chain(ctx.groups);
        free(report);
        return NULL;
    }
    
    /* Populate report */
    report->total_leaks = ctx.total_leaks;
    report->total_bytes = ctx.total_bytes;
    report->group_count = ctx.group_count;
    report->severity = compute_severity(ctx.total_bytes);
    report->groups = ctx.groups;
    report->detection_time_us = get_time_us() - start_time;
    report->suppression_applied = false;
    
    return report;
}

void leak_report_destroy(leak_report_t* report) {
    if (report == NULL) {
        return;
    }
    
    internal_leak_group_destroy_chain(report->groups);
    free(report);
}

bool leak_report_has_leaks(const leak_report_t* report) {
    if (report == NULL) {
        return false;
    }
    return report->total_leaks > 0;
}

const char* leak_severity_to_string(leak_severity_t severity) {
    switch (severity) {
        case LEAK_SEVERITY_NONE:     return "None";
        case LEAK_SEVERITY_LOW:      return "Low";
        case LEAK_SEVERITY_MEDIUM:   return "Medium";
        case LEAK_SEVERITY_HIGH:     return "High";
        case LEAK_SEVERITY_CRITICAL: return "Critical";
        default:                     return "Unknown";
    }
}

const leak_group_t* leak_report_largest_group(const leak_report_t* report) {
    if (report == NULL || report->groups == NULL) {
        return NULL;
    }
    
    const leak_group_t* largest = NULL;
    size_t largest_bytes = 0;
    
    const leak_group_t* group = report->groups;
    while (group != NULL) {
        if (group->total_bytes > largest_bytes) {
            largest_bytes = group->total_bytes;
            largest = group;
        }
        group = group->next;
    }
    
    return largest;
}

const leak_group_t* leak_report_most_frequent_group(const leak_report_t* report) {
    if (report == NULL || report->groups == NULL) {
        return NULL;
    }
    
    const leak_group_t* most_frequent = NULL;
    size_t most_count = 0;
    
    const leak_group_t* group = report->groups;
    while (group != NULL) {
        if (group->leak_count > most_count) {
            most_count = group->leak_count;
            most_frequent = group;
        }
        group = group->next;
    }
    
    return most_frequent;
}

/* ============================================================================
 * Backtrace Signature Functions
 * ============================================================================ */

uint64_t backtrace_compute_signature(void* const* frames, int frame_count) {
    if (frames == NULL || frame_count <= 0) {
        return 0;
    }
    
    /* FNV-1a hash algorithm for good distribution */
    uint64_t hash = 14695981039346656037ULL;  /* FNV offset basis */
    const uint64_t prime = 1099511628211ULL;  /* FNV prime */
    
    for (int i = 0; i < frame_count; i++) {
        uintptr_t addr = (uintptr_t)frames[i];
        
        /* Hash each byte of the address */
        for (size_t b = 0; b < sizeof(uintptr_t); b++) {
            hash ^= (addr & 0xFF);
            hash *= prime;
            addr >>= 8;
        }
    }
    
    return hash;
}

bool backtrace_equals(void* const* frames1, int count1,
                      void* const* frames2, int count2) {
    if (count1 != count2) {
        return false;
    }
    
    if (count1 == 0) {
        return true;  /* Both empty */
    }
    
    if (frames1 == NULL || frames2 == NULL) {
        return frames1 == frames2;
    }
    
    return memcmp(frames1, frames2, (size_t)count1 * sizeof(void*)) == 0;
}

/* ============================================================================
 * Leak Entry Lifecycle Functions (MEMRO-18)
 * ============================================================================ */

leak_entry_t* leak_entry_create(void* address, size_t size,
                                 const char* file, int line,
                                 bool copy_strings) {
    leak_entry_t* entry = calloc(1, sizeof(leak_entry_t));
    if (entry == NULL) {
        return NULL;
    }
    
    entry->address = address;
    entry->size = size;
    entry->line = line;
    entry->timestamp = 0;
    entry->frame_count = 0;
    entry->next = NULL;
    entry->owns_file = false;
    
    if (copy_strings && file != NULL) {
        entry->file = string_dup(file);
        if (entry->file == NULL) {
            free(entry);
            return NULL;
        }
        entry->owns_file = true;
    } else {
        entry->file = file;
    }
    
    return entry;
}

leak_entry_t* leak_entry_clone(const leak_entry_t* entry) {
    if (entry == NULL) {
        return NULL;
    }
    
    leak_entry_t* clone = calloc(1, sizeof(leak_entry_t));
    if (clone == NULL) {
        return NULL;
    }
    
    clone->address = entry->address;
    clone->size = entry->size;
    clone->line = entry->line;
    clone->timestamp = entry->timestamp;
    clone->frame_count = entry->frame_count;
    clone->next = NULL;
    clone->owns_file = false;
    
    /* Copy string - clone always owns its string */
    if (entry->file != NULL) {
        clone->file = string_dup(entry->file);
        if (clone->file == NULL) {
            free(clone);
            return NULL;
        }
        clone->owns_file = true;
    } else {
        clone->file = NULL;
    }
    
    /* Copy backtrace */
    if (entry->frame_count > 0) {
        memcpy(clone->frames, entry->frames,
               (size_t)entry->frame_count * sizeof(void*));
    }
    
    return clone;
}

void leak_entry_destroy(leak_entry_t* entry) {
    if (entry == NULL) {
        return;
    }
    /* Free file string if we own it (allocated by clone or copy_strings) */
    if (entry->owns_file && entry->file != NULL) {
        free((void*)entry->file);
    }
    free(entry);
}

void leak_entry_destroy_chain(leak_entry_t* entry) {
    while (entry != NULL) {
        leak_entry_t* next = entry->next;
        leak_entry_destroy(entry);
        entry = next;
    }
}

void leak_entry_set_backtrace(leak_entry_t* entry,
                               void* const* frames, int frame_count) {
    if (entry == NULL) {
        return;
    }
    
    if (frames == NULL || frame_count <= 0) {
        entry->frame_count = 0;
        return;
    }
    
    int count = frame_count;
    if (count > MEMROGUE_MAX_FRAMES) {
        count = MEMROGUE_MAX_FRAMES;
    }
    
    memcpy(entry->frames, frames, (size_t)count * sizeof(void*));
    entry->frame_count = count;
}

size_t leak_entry_chain_count(const leak_entry_t* entry) {
    size_t count = 0;
    while (entry != NULL) {
        count++;
        entry = entry->next;
    }
    return count;
}

/* ============================================================================
 * Leak Group Lifecycle Functions (MEMRO-18)
 * ============================================================================ */

leak_group_t* leak_group_create(const char* file, int line, bool copy_strings) {
    leak_group_t* group = calloc(1, sizeof(leak_group_t));
    if (group == NULL) {
        return NULL;
    }
    
    group->signature = 0;
    group->frame_count = 0;
    group->line = line;
    group->leak_count = 0;
    group->total_bytes = 0;
    group->entries = NULL;
    group->next = NULL;
    group->owns_file = false;
    
    if (copy_strings && file != NULL) {
        group->file = string_dup(file);
        if (group->file == NULL) {
            free(group);
            return NULL;
        }
        group->owns_file = true;
    } else {
        group->file = file;
    }
    
    return group;
}

leak_group_t* leak_group_clone(const leak_group_t* group) {
    if (group == NULL) {
        return NULL;
    }
    
    leak_group_t* clone = calloc(1, sizeof(leak_group_t));
    if (clone == NULL) {
        return NULL;
    }
    
    clone->signature = group->signature;
    clone->frame_count = group->frame_count;
    clone->line = group->line;
    clone->leak_count = group->leak_count;
    clone->total_bytes = group->total_bytes;
    clone->entries = NULL;
    clone->next = NULL;
    clone->owns_file = false;
    
    /* Copy string - clone always owns its string */
    if (group->file != NULL) {
        clone->file = string_dup(group->file);
        if (clone->file == NULL) {
            free(clone);
            return NULL;
        }
        clone->owns_file = true;
    } else {
        clone->file = NULL;
    }
    
    /* Copy backtrace */
    if (group->frame_count > 0) {
        memcpy(clone->frames, group->frames,
               (size_t)group->frame_count * sizeof(void*));
    }
    
    /* Clone entries */
    const leak_entry_t* src_entry = group->entries;
    leak_entry_t** tail = &clone->entries;
    while (src_entry != NULL) {
        leak_entry_t* entry_clone = leak_entry_clone(src_entry);
        if (entry_clone == NULL) {
            leak_group_destroy(clone);
            return NULL;
        }
        *tail = entry_clone;
        tail = &entry_clone->next;
        src_entry = src_entry->next;
    }
    
    return clone;
}

void leak_group_destroy(leak_group_t* group) {
    if (group == NULL) {
        return;
    }
    
    /* Destroy all entries in the chain */
    leak_entry_destroy_chain(group->entries);
    
    /* Free file string if we own it (allocated by clone or copy_strings) */
    if (group->owns_file && group->file != NULL) {
        free((void*)group->file);
    }
    free(group);
}

void leak_group_destroy_chain(leak_group_t* group) {
    while (group != NULL) {
        leak_group_t* next = group->next;
        leak_group_destroy(group);
        group = next;
    }
}

bool leak_group_add_entry(leak_group_t* group, leak_entry_t* entry) {
    if (group == NULL || entry == NULL) {
        return false;
    }
    
    /* Prepend entry to list */
    entry->next = group->entries;
    group->entries = entry;
    
    /* Update statistics */
    group->leak_count++;
    group->total_bytes += entry->size;
    
    return true;
}

leak_entry_t* leak_group_pop_entry(leak_group_t* group) {
    if (group == NULL || group->entries == NULL) {
        return NULL;
    }
    
    leak_entry_t* entry = group->entries;
    group->entries = entry->next;
    entry->next = NULL;
    
    /* Update statistics */
    if (group->leak_count > 0) {
        group->leak_count--;
    }
    if (group->total_bytes >= entry->size) {
        group->total_bytes -= entry->size;
    } else {
        group->total_bytes = 0;  /* Prevent underflow */
    }
    
    return entry;
}

void leak_group_set_backtrace(leak_group_t* group,
                               void* const* frames, int frame_count) {
    if (group == NULL) {
        return;
    }
    
    if (frames == NULL || frame_count <= 0) {
        group->frame_count = 0;
        group->signature = 0;
        return;
    }
    
    int count = frame_count;
    if (count > MEMROGUE_MAX_FRAMES) {
        count = MEMROGUE_MAX_FRAMES;
    }
    
    memcpy(group->frames, frames, (size_t)count * sizeof(void*));
    group->frame_count = count;
    
    /* Recompute signature */
    group->signature = backtrace_compute_signature(group->frames, count);
}

void leak_group_recalculate_stats(leak_group_t* group) {
    if (group == NULL) {
        return;
    }
    
    group->leak_count = 0;
    group->total_bytes = 0;
    
    const leak_entry_t* entry = group->entries;
    while (entry != NULL) {
        group->leak_count++;
        group->total_bytes += entry->size;
        entry = entry->next;
    }
}

/* ============================================================================
 * Leak Report Lifecycle Functions (MEMRO-18)
 * ============================================================================ */

leak_report_t* leak_report_create(void) {
    leak_report_t* report = calloc(1, sizeof(leak_report_t));
    if (report == NULL) {
        return NULL;
    }
    
    report->total_leaks = 0;
    report->total_bytes = 0;
    report->group_count = 0;
    report->severity = LEAK_SEVERITY_NONE;
    report->groups = NULL;
    report->detection_time_us = 0;
    report->suppression_applied = false;
    
    return report;
}

leak_report_t* leak_report_clone(const leak_report_t* report) {
    if (report == NULL) {
        return NULL;
    }
    
    leak_report_t* clone = calloc(1, sizeof(leak_report_t));
    if (clone == NULL) {
        return NULL;
    }
    
    clone->total_leaks = report->total_leaks;
    clone->total_bytes = report->total_bytes;
    clone->group_count = report->group_count;
    clone->severity = report->severity;
    clone->detection_time_us = report->detection_time_us;
    clone->suppression_applied = report->suppression_applied;
    clone->groups = NULL;
    
    /* Clone groups */
    const leak_group_t* src_group = report->groups;
    leak_group_t** tail = &clone->groups;
    while (src_group != NULL) {
        leak_group_t* group_clone = leak_group_clone(src_group);
        if (group_clone == NULL) {
            leak_report_destroy(clone);
            return NULL;
        }
        *tail = group_clone;
        tail = &group_clone->next;
        src_group = src_group->next;
    }
    
    return clone;
}

bool leak_report_add_group(leak_report_t* report, leak_group_t* group) {
    if (report == NULL || group == NULL) {
        return false;
    }
    
    /* Prepend group to list */
    group->next = report->groups;
    report->groups = group;
    
    /* Update statistics */
    report->group_count++;
    report->total_leaks += group->leak_count;
    report->total_bytes += group->total_bytes;
    report->severity = compute_severity(report->total_bytes);
    
    return true;
}

leak_group_t* leak_report_pop_group(leak_report_t* report) {
    if (report == NULL || report->groups == NULL) {
        return NULL;
    }
    
    leak_group_t* group = report->groups;
    report->groups = group->next;
    group->next = NULL;
    
    /* Update statistics */
    if (report->group_count > 0) {
        report->group_count--;
    }
    if (report->total_leaks >= group->leak_count) {
        report->total_leaks -= group->leak_count;
    } else {
        report->total_leaks = 0;
    }
    if (report->total_bytes >= group->total_bytes) {
        report->total_bytes -= group->total_bytes;
    } else {
        report->total_bytes = 0;
    }
    report->severity = compute_severity(report->total_bytes);
    
    return group;
}

leak_group_t* leak_report_find_group_by_signature(leak_report_t* report,
                                                   uint64_t signature) {
    if (report == NULL) {
        return NULL;
    }
    
    leak_group_t* group = report->groups;
    while (group != NULL) {
        if (group->signature == signature) {
            return group;
        }
        group = group->next;
    }
    
    return NULL;
}

void leak_report_recalculate_stats(leak_report_t* report) {
    if (report == NULL) {
        return;
    }
    
    report->total_leaks = 0;
    report->total_bytes = 0;
    report->group_count = 0;
    
    const leak_group_t* group = report->groups;
    while (group != NULL) {
        report->group_count++;
        report->total_leaks += group->leak_count;
        report->total_bytes += group->total_bytes;
        group = group->next;
    }
    
    report->severity = compute_severity(report->total_bytes);
}

bool leak_report_merge(leak_report_t* dest, const leak_report_t* src) {
    if (dest == NULL || src == NULL) {
        return false;
    }
    
    const leak_group_t* src_group = src->groups;
    while (src_group != NULL) {
        /* Try to find matching group in dest by signature */
        leak_group_t* dest_group = NULL;
        if (src_group->signature != 0) {
            dest_group = leak_report_find_group_by_signature(dest, src_group->signature);
        }
        
        if (dest_group != NULL) {
            /* Merge entries into existing group */
            const leak_entry_t* src_entry = src_group->entries;
            while (src_entry != NULL) {
                leak_entry_t* entry_clone = leak_entry_clone(src_entry);
                if (entry_clone == NULL) {
                    /* Allocation failure - dest may have partial merge state.
                     * Caller should destroy dest if atomicity is required. */
                    return false;
                }
                
                /* Add entry (updates statistics) */
                if (!leak_group_add_entry(dest_group, entry_clone)) {
                    leak_entry_destroy(entry_clone);
                    return false;
                }
                
                src_entry = src_entry->next;
            }
            
            /* Update report totals for merged entries */
            dest->total_leaks += src_group->leak_count;
            dest->total_bytes += src_group->total_bytes;
        } else {
            /* Clone entire group and add to dest */
            leak_group_t* group_clone = leak_group_clone(src_group);
            if (group_clone == NULL) {
                /* Allocation failure - dest may have partial merge state.
                 * Caller should destroy dest if atomicity is required. */
                return false;
            }
            
            if (!leak_report_add_group(dest, group_clone)) {
                leak_group_destroy(group_clone);
                return false;
            }
        }
        
        src_group = src_group->next;
    }
    
    /* Recalculate severity */
    dest->severity = compute_severity(dest->total_bytes);
    
    return true;
}

void leak_report_clear(leak_report_t* report) {
    if (report == NULL) {
        return;
    }
    
    leak_group_destroy_chain(report->groups);
    report->groups = NULL;
    report->total_leaks = 0;
    report->total_bytes = 0;
    report->group_count = 0;
    report->severity = LEAK_SEVERITY_NONE;
}

int leak_report_iterate_entries(const leak_report_t* report,
                                 leak_entry_iterator_fn callback,
                                 void* user_data) {
    if (report == NULL || callback == NULL) {
        return 0;
    }
    
    int count = 0;
    const leak_group_t* group = report->groups;
    
    while (group != NULL) {
        const leak_entry_t* entry = group->entries;
        while (entry != NULL) {
            count++;
            if (!callback(entry, group, user_data)) {
                return -1;  /* Callback requested stop */
            }
            entry = entry->next;
        }
        group = group->next;
    }
    
    return count;
}
