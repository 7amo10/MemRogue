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
 *
 * MEMRO-14: Leak Detection Engine
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
 * Create a new leak entry from allocation info.
 * Returns NULL on allocation failure.
 */
static leak_entry_t* leak_entry_create(const allocation_info_t* info) {
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
 * Destroy a leak entry and all entries in its linked list.
 */
static void leak_entry_destroy_chain(leak_entry_t* entry) {
    while (entry != NULL) {
        leak_entry_t* next = entry->next;
        free(entry);
        entry = next;
    }
}

/**
 * Create a new leak group with the given signature.
 * Returns NULL on allocation failure.
 */
static leak_group_t* leak_group_create(uint64_t signature, 
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
    group->file = first_info->file;
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
 * Destroy a leak group and all its entries.
 */
static void leak_group_destroy(leak_group_t* group) {
    if (group == NULL) {
        return;
    }
    leak_entry_destroy_chain(group->entries);
    free(group);
}

/**
 * Destroy a linked list of leak groups.
 */
static void leak_group_destroy_chain(leak_group_t* group) {
    while (group != NULL) {
        leak_group_t* next = group->next;
        leak_group_destroy(group);
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
    leak_group_t* new_group = leak_group_create(signature, info);
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
    leak_entry_t* entry = leak_entry_create(info);
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
        group = leak_group_create(ctx->total_leaks, info);
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
        leak_group_destroy_chain(ctx.groups);
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
    
    leak_group_destroy_chain(report->groups);
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
