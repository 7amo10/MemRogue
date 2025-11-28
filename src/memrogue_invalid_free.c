/**
 * @file memrogue_invalid_free.c
 * @brief Implementation of invalid free detection.
 *
 * Tracks currently allocated pointers in a hash table. When a free is attempted,
 * checks if the pointer is in the set of tracked allocations. If not found,
 * reports an invalid free violation.
 *
 * Implementation Details:
 * - Hash table for O(1) allocation tracking
 * - Per-structure locking for thread safety
 * - Copy-on-store for file names to avoid dangling pointers
 * - Configurable severity levels for different response behaviors
 * - Distinguishes from double-free (complements MEMRO-15)
 *
 * MEMRO-16: Invalid Free Detection
 */

/* Ensure POSIX features are available (must be before any includes) */
#define _POSIX_C_SOURCE 200809L  /* For strdup and clock_gettime */
#define _DEFAULT_SOURCE          /* For strdup on older glibc */

#include "memrogue_invalid_free.h"
#include "memrogue_backtrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>

#if defined(__GLIBC__) || defined(__APPLE__)
#define HAVE_BACKTRACE 1
#include <execinfo.h>
#else
#define HAVE_BACKTRACE 0
#endif

/* ============================================================================
 * Internal Backtrace Capture
 * ============================================================================ */

/**
 * Capture backtrace directly into a raw array.
 * 
 * @param frames Output array for frame addresses
 * @param max_frames Maximum number of frames to capture
 * @param skip_frames Number of frames to skip from top of stack
 * @return Number of frames captured
 */
static int capture_backtrace_raw(void** frames, int max_frames, int skip_frames) {
    if (!frames || max_frames <= 0) {
        return 0;
    }
    
    memset(frames, 0, sizeof(void*) * (size_t)max_frames);
    
#if HAVE_BACKTRACE
    void* temp_buffer[MEMROGUE_MAX_FRAMES + 16];
    int max_capture = max_frames + skip_frames;
    
    if (max_capture > (int)(sizeof(temp_buffer) / sizeof(temp_buffer[0]))) {
        max_capture = (int)(sizeof(temp_buffer) / sizeof(temp_buffer[0]));
    }
    
    int total_frames = backtrace(temp_buffer, max_capture);
    
    if (total_frames <= 0) {
        return 0;
    }
    
    int frames_to_copy = total_frames - skip_frames;
    if (frames_to_copy <= 0) {
        return 0;
    }
    
    if (frames_to_copy > max_frames) {
        frames_to_copy = max_frames;
    }
    
    for (int i = 0; i < frames_to_copy; i++) {
        frames[i] = temp_buffer[skip_frames + i];
    }
    
    return frames_to_copy;
#else
    (void)skip_frames;
    return 0;
#endif
}

/**
 * Convert backtrace frames to symbol strings.
 * 
 * @param frames Array of frame addresses
 * @param frame_count Number of frames
 * @return Array of symbol strings (caller must free with free())
 */
static char** backtrace_to_symbols_raw(void* const* frames, int frame_count) {
    if (!frames || frame_count <= 0) {
        return NULL;
    }
    
#if HAVE_BACKTRACE
    return backtrace_symbols((void* const*)frames, frame_count);
#else
    return NULL;
#endif
}

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * Entry for a tracked allocation.
 */
typedef struct alloc_entry {
    void* ptr;                                  /**< The allocated pointer (key) */
    size_t size;                                /**< Allocation size */
    char* file;                                 /**< Owned copy of file name */
    int line;                                   /**< Line number */
    uint64_t timestamp;                         /**< When allocated */
    void* frames[MEMROGUE_MAX_FRAMES];          /**< Backtrace frames */
    int frame_count;                            /**< Number of frames */
    struct alloc_entry* next;                   /**< Next in hash bucket */
} alloc_entry_t;

/**
 * Hash bucket for allocation tracking.
 */
typedef struct {
    alloc_entry_t* head;                        /**< Head of bucket chain */
} alloc_bucket_t;

/**
 * Internal invalid free detector structure.
 */
struct invalid_free_detector_internal {
    /* Configuration */
    invalid_free_config_t config;
    
    /* Allocation tracking (hash table) */
    alloc_bucket_t* buckets;                    /**< Hash buckets */
    size_t bucket_count;                        /**< Number of buckets */
    size_t alloc_count;                         /**< Current number of allocations */
    size_t peak_alloc_count;                    /**< Peak allocation count */
    pthread_mutex_t alloc_lock;                 /**< Lock for allocation table */
    
    /* Violation handling */
    invalid_free_callback_t callback;           /**< User callback */
    void* callback_user_data;                   /**< Callback context */
    invalid_free_violation_t last_violation;    /**< Most recent violation */
    bool has_violation;                         /**< Whether last_violation is valid */
    char* last_violation_context;               /**< Owned context string */
    char* last_violation_file;                  /**< Owned file string for violation */
    pthread_mutex_t violation_lock;             /**< Lock for violation data */
    
    /* Statistics */
    invalid_free_stats_t stats;
    pthread_mutex_t stats_lock;                 /**< Lock for statistics */
    
    /* State */
    bool initialized;
};

/* ============================================================================
 * Hash Function
 * ============================================================================ */

/**
 * Simple hash function for pointers.
 * Uses FNV-1a algorithm for good distribution.
 */
static size_t hash_ptr(void* ptr, size_t bucket_count) {
    uintptr_t addr = (uintptr_t)ptr;
    
    /* FNV-1a hash */
    uint64_t hash = 14695981039346656037ULL;
    for (int i = 0; i < (int)sizeof(uintptr_t); i++) {
        hash ^= (addr >> (i * 8)) & 0xFF;
        hash *= 1099511628211ULL;
    }
    
    return (size_t)(hash % bucket_count);
}

/* ============================================================================
 * Timestamp
 * ============================================================================ */

/**
 * Get current timestamp in microseconds.
 */
static uint64_t get_timestamp_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    }
    return 0;
}

/* ============================================================================
 * String Utilities
 * ============================================================================ */

/**
 * Duplicate a string if not NULL.
 */
static char* safe_strdup(const char* str) {
    if (!str) {
        return NULL;
    }
    return strdup(str);
}

/* ============================================================================
 * Allocation Entry Management
 * ============================================================================ */

/**
 * Create a new allocation entry.
 */
static alloc_entry_t* alloc_entry_create(void* ptr, size_t size,
                                         const char* file, int line,
                                         uint64_t timestamp) {
    alloc_entry_t* entry = calloc(1, sizeof(alloc_entry_t));
    if (!entry) {
        return NULL;
    }
    
    entry->ptr = ptr;
    entry->size = size;
    entry->file = safe_strdup(file);
    entry->line = line;
    entry->timestamp = timestamp;
    
    return entry;
}

/**
 * Destroy an allocation entry.
 */
static void alloc_entry_destroy(alloc_entry_t* entry) {
    if (!entry) {
        return;
    }
    
    free(entry->file);
    free(entry);
}

/* ============================================================================
 * Hash Table Operations
 * ============================================================================ */

/**
 * Find an allocation entry in the hash table.
 * Caller must hold alloc_lock.
 */
static alloc_entry_t* alloc_find(invalid_free_detector_t* detector, void* ptr) {
    size_t bucket = hash_ptr(ptr, detector->bucket_count);
    alloc_entry_t* entry = detector->buckets[bucket].head;
    
    while (entry) {
        if (entry->ptr == ptr) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/**
 * Add an allocation entry to the hash table.
 * Caller must hold alloc_lock.
 */
static void alloc_add(invalid_free_detector_t* detector, alloc_entry_t* entry) {
    size_t bucket = hash_ptr(entry->ptr, detector->bucket_count);
    entry->next = detector->buckets[bucket].head;
    detector->buckets[bucket].head = entry;
    
    detector->alloc_count++;
    if (detector->alloc_count > detector->peak_alloc_count) {
        detector->peak_alloc_count = detector->alloc_count;
    }
}

/**
 * Remove and return an allocation entry from the hash table.
 * Caller must hold alloc_lock.
 */
static alloc_entry_t* alloc_remove(invalid_free_detector_t* detector, void* ptr) {
    size_t bucket = hash_ptr(ptr, detector->bucket_count);
    alloc_entry_t** pp = &detector->buckets[bucket].head;
    
    while (*pp) {
        if ((*pp)->ptr == ptr) {
            alloc_entry_t* entry = *pp;
            *pp = entry->next;
            entry->next = NULL;
            detector->alloc_count--;
            return entry;
        }
        pp = &(*pp)->next;
    }
    
    return NULL;
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

void invalid_free_config_init(invalid_free_config_t* config) {
    if (!config) {
        return;
    }
    
    config->enabled = true;
    config->severity = INVALID_FREE_SEVERITY_ERROR;
    config->print_on_error = true;
    config->initial_capacity = INVALID_FREE_DEFAULT_CAPACITY;
    config->backtrace_skip_frames = 2;
}

/* ============================================================================
 * Detector Lifecycle
 * ============================================================================ */

invalid_free_detector_t* invalid_free_detector_create(void) {
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    return invalid_free_detector_create_with_config(&config);
}

invalid_free_detector_t* invalid_free_detector_create_with_config(
    const invalid_free_config_t* config) {
    if (!config) {
        return NULL;
    }
    
    invalid_free_detector_t* detector = calloc(1, sizeof(invalid_free_detector_t));
    if (!detector) {
        return NULL;
    }
    
    /* Copy configuration */
    detector->config = *config;
    
    /* Calculate bucket count (use prime-ish number for better distribution) */
    detector->bucket_count = (config->initial_capacity / 4) | 1; /* Ensure odd */
    if (detector->bucket_count < 17) {
        detector->bucket_count = 17;
    }
    
    /* Allocate hash table */
    detector->buckets = calloc(detector->bucket_count, sizeof(alloc_bucket_t));
    if (!detector->buckets) {
        goto fail;
    }
    
    /* Initialize locks */
    if (pthread_mutex_init(&detector->alloc_lock, NULL) != 0) {
        goto fail;
    }
    
    if (pthread_mutex_init(&detector->violation_lock, NULL) != 0) {
        pthread_mutex_destroy(&detector->alloc_lock);
        goto fail;
    }
    
    if (pthread_mutex_init(&detector->stats_lock, NULL) != 0) {
        pthread_mutex_destroy(&detector->alloc_lock);
        pthread_mutex_destroy(&detector->violation_lock);
        goto fail;
    }
    
    detector->initialized = true;
    return detector;
    
fail:
    free(detector->buckets);
    free(detector);
    return NULL;
}

void invalid_free_detector_destroy(invalid_free_detector_t* detector) {
    if (!detector) {
        return;
    }
    
    /* Free all allocation entries */
    for (size_t i = 0; i < detector->bucket_count; i++) {
        alloc_entry_t* entry = detector->buckets[i].head;
        while (entry) {
            alloc_entry_t* next = entry->next;
            alloc_entry_destroy(entry);
            entry = next;
        }
    }
    
    /* Free hash table */
    free(detector->buckets);
    
    /* Free owned strings in violation data */
    free(detector->last_violation_context);
    free(detector->last_violation_file);
    
    /* Destroy locks */
    pthread_mutex_destroy(&detector->alloc_lock);
    pthread_mutex_destroy(&detector->violation_lock);
    pthread_mutex_destroy(&detector->stats_lock);
    
    detector->initialized = false;
    free(detector);
}

/* ============================================================================
 * Detection API
 * ============================================================================ */

void invalid_free_record_alloc(invalid_free_detector_t* detector, void* ptr,
                               size_t size, const char* file, int line) {
    if (!detector || !detector->initialized || !ptr) {
        return;
    }
    
    uint64_t timestamp = get_timestamp_us();
    
    alloc_entry_t* entry = alloc_entry_create(ptr, size, file, line, timestamp);
    if (!entry) {
        return;
    }
    
    /* Capture backtrace */
    entry->frame_count = capture_backtrace_raw(entry->frames, MEMROGUE_MAX_FRAMES,
                                               detector->config.backtrace_skip_frames);
    
    pthread_mutex_lock(&detector->alloc_lock);
    
    /* Remove any existing entry for this pointer (reuse case) */
    alloc_entry_t* old = alloc_remove(detector, ptr);
    if (old) {
        alloc_entry_destroy(old);
    }
    
    alloc_add(detector, entry);
    
    /* Capture alloc_count under alloc_lock to avoid race */
    size_t alloc_count_snapshot = detector->alloc_count;
    pthread_mutex_unlock(&detector->alloc_lock);
    
    /* Update stats */
    pthread_mutex_lock(&detector->stats_lock);
    detector->stats.allocs_recorded++;
    detector->stats.current_allocations = alloc_count_snapshot;
    if (alloc_count_snapshot > detector->stats.peak_allocations) {
        detector->stats.peak_allocations = alloc_count_snapshot;
    }
    pthread_mutex_unlock(&detector->stats_lock);
}

/**
 * Build a violation report.
 * Caller holds violation_lock.
 */
static void build_violation(invalid_free_detector_t* detector,
                           void* ptr, invalid_free_type_t type,
                           const char* file, int line,
                           uint64_t timestamp,
                           void* frames[], int frame_count,
                           const char* context) {
    invalid_free_violation_t* v = &detector->last_violation;
    
    /* Free previous owned strings */
    free(detector->last_violation_file);
    free(detector->last_violation_context);
    
    /* Copy file and context strings (take ownership) */
    detector->last_violation_file = safe_strdup(file);
    detector->last_violation_context = safe_strdup(context);
    
    /* Clear and fill violation */
    memset(v, 0, sizeof(*v));
    
    v->address = ptr;
    v->type = type;
    v->free_file = detector->last_violation_file;
    v->free_line = line;
    v->free_timestamp = timestamp;
    memcpy(v->free_frames, frames, sizeof(void*) * (size_t)frame_count);
    v->free_frame_count = frame_count;
    v->context = detector->last_violation_context;
    
    detector->has_violation = true;
}

bool invalid_free_check_and_remove(invalid_free_detector_t* detector, void* ptr,
                                   const char* file, int line) {
    if (!detector || !detector->initialized || !ptr) {
        return true; /* No detection = valid free */
    }
    
    /* Read enabled flag (protected by stats_lock) */
    pthread_mutex_lock(&detector->stats_lock);
    bool enabled = detector->config.enabled;
    pthread_mutex_unlock(&detector->stats_lock);
    
    if (!enabled) {
        /* Still remove from tracking even when disabled */
        pthread_mutex_lock(&detector->alloc_lock);
        alloc_entry_t* entry = alloc_remove(detector, ptr);
        size_t current_count = detector->alloc_count;
        pthread_mutex_unlock(&detector->alloc_lock);
        
        if (entry) {
            alloc_entry_destroy(entry);
            
            pthread_mutex_lock(&detector->stats_lock);
            detector->stats.frees_recorded++;
            detector->stats.current_allocations = current_count;
            pthread_mutex_unlock(&detector->stats_lock);
        }
        return true;
    }
    
    uint64_t timestamp = get_timestamp_us();
    
    /* Capture backtrace for this free */
    void* frames[MEMROGUE_MAX_FRAMES];
    int frame_count = capture_backtrace_raw(frames, MEMROGUE_MAX_FRAMES,
                                            detector->config.backtrace_skip_frames);
    
    /* Try to remove from tracked allocations */
    pthread_mutex_lock(&detector->alloc_lock);
    alloc_entry_t* entry = alloc_remove(detector, ptr);
    size_t current_count = detector->alloc_count;
    pthread_mutex_unlock(&detector->alloc_lock);
    
    if (entry) {
        /* Valid free - pointer was tracked */
        alloc_entry_destroy(entry);
        
        pthread_mutex_lock(&detector->stats_lock);
        detector->stats.frees_recorded++;
        detector->stats.current_allocations = current_count;
        pthread_mutex_unlock(&detector->stats_lock);
        
        return true;
    }
    
    /* Invalid free detected - pointer was not tracked */
    
    /* Variables for callback invocation outside locks */
    invalid_free_callback_t callback = NULL;
    void* callback_data = NULL;
    invalid_free_violation_t violation_copy;
    invalid_free_severity_t severity;
    bool print_on_error;
    
    /* Build violation report */
    pthread_mutex_lock(&detector->violation_lock);
    build_violation(detector, ptr, INVALID_FREE_TYPE_UNTRACKED,
                   file, line, timestamp, frames, frame_count,
                   "Pointer was never allocated or not tracked");
    
    /* Copy for callback (to call outside lock) */
    violation_copy = detector->last_violation;
    callback = detector->callback;
    callback_data = detector->callback_user_data;
    pthread_mutex_unlock(&detector->violation_lock);
    
    /* Read configuration under lock */
    pthread_mutex_lock(&detector->stats_lock);
    severity = detector->config.severity;
    print_on_error = detector->config.print_on_error;
    detector->stats.invalid_frees_detected++;
    detector->stats.untracked_frees++;
    pthread_mutex_unlock(&detector->stats_lock);
    
    /* Call callback outside of locks to prevent deadlock */
    if (callback) {
        callback(&violation_copy, callback_data);
    }
    
    /* Print if configured */
    if (print_on_error) {
        invalid_free_print_violation(&violation_copy);
    }
    
    /* Abort if severity is FATAL */
    if (severity == INVALID_FREE_SEVERITY_FATAL) {
        abort();
    }
    
    return false;
}

void invalid_free_record_realloc(invalid_free_detector_t* detector,
                                 void* old_ptr, void* new_ptr,
                                 size_t new_size, const char* file, int line) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    uint64_t timestamp = get_timestamp_us();
    
    pthread_mutex_lock(&detector->alloc_lock);
    
    /* Remove old pointer if tracked */
    if (old_ptr) {
        alloc_entry_t* old = alloc_remove(detector, old_ptr);
        if (old) {
            alloc_entry_destroy(old);
        }
    }
    
    /* Add new pointer */
    if (new_ptr) {
        /* Remove any existing entry for new_ptr (address reuse case) */
        alloc_entry_t* existing = alloc_remove(detector, new_ptr);
        if (existing) {
            alloc_entry_destroy(existing);
        }
        
        alloc_entry_t* entry = alloc_entry_create(new_ptr, new_size, file, line, timestamp);
        if (entry) {
            entry->frame_count = capture_backtrace_raw(entry->frames, MEMROGUE_MAX_FRAMES,
                                                       detector->config.backtrace_skip_frames);
            alloc_add(detector, entry);
        }
    }
    
    size_t current_count = detector->alloc_count;
    pthread_mutex_unlock(&detector->alloc_lock);
    
    /* Update stats */
    pthread_mutex_lock(&detector->stats_lock);
    detector->stats.current_allocations = current_count;
    if (current_count > detector->stats.peak_allocations) {
        detector->stats.peak_allocations = current_count;
    }
    pthread_mutex_unlock(&detector->stats_lock);
}

bool invalid_free_is_tracked(invalid_free_detector_t* detector, void* ptr) {
    if (!detector || !detector->initialized || !ptr) {
        return false;
    }
    
    pthread_mutex_lock(&detector->alloc_lock);
    alloc_entry_t* entry = alloc_find(detector, ptr);
    pthread_mutex_unlock(&detector->alloc_lock);
    
    return entry != NULL;
}

/* ============================================================================
 * Violation Handling
 * ============================================================================ */

void invalid_free_set_callback(invalid_free_detector_t* detector,
                               invalid_free_callback_t callback,
                               void* user_data) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    pthread_mutex_lock(&detector->violation_lock);
    detector->callback = callback;
    detector->callback_user_data = user_data;
    pthread_mutex_unlock(&detector->violation_lock);
}

bool invalid_free_get_last_violation(
    invalid_free_detector_t* detector,
    invalid_free_violation_t* out_violation) {
    if (!detector || !detector->initialized || !out_violation) {
        return false;
    }
    
    pthread_mutex_lock(&detector->violation_lock);
    if (!detector->has_violation) {
        pthread_mutex_unlock(&detector->violation_lock);
        return false;
    }
    *out_violation = detector->last_violation;
    pthread_mutex_unlock(&detector->violation_lock);
    return true;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

void invalid_free_get_stats(invalid_free_detector_t* detector,
                            invalid_free_stats_t* out_stats) {
    if (!detector || !detector->initialized || !out_stats) {
        if (out_stats) {
            memset(out_stats, 0, sizeof(*out_stats));
        }
        return;
    }
    
    pthread_mutex_lock(&detector->stats_lock);
    *out_stats = detector->stats;
    pthread_mutex_unlock(&detector->stats_lock);
    
    /* Update current_allocations from actual count */
    pthread_mutex_lock(&detector->alloc_lock);
    out_stats->current_allocations = detector->alloc_count;
    pthread_mutex_unlock(&detector->alloc_lock);
}

void invalid_free_reset_stats(invalid_free_detector_t* detector) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    /* Lock stats first, then alloc for consistent ordering */
    pthread_mutex_lock(&detector->stats_lock);
    memset(&detector->stats, 0, sizeof(detector->stats));
    pthread_mutex_unlock(&detector->stats_lock);
    
    /* Update with current allocation count */
    pthread_mutex_lock(&detector->alloc_lock);
    size_t count = detector->alloc_count;
    pthread_mutex_unlock(&detector->alloc_lock);
    
    pthread_mutex_lock(&detector->stats_lock);
    detector->stats.current_allocations = count;
    detector->stats.peak_allocations = count;
    pthread_mutex_unlock(&detector->stats_lock);
}

/* ============================================================================
 * Configuration Updates
 * ============================================================================ */

void invalid_free_set_enabled(invalid_free_detector_t* detector, bool enabled) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    /* Use stats_lock for thread-safe config update */
    pthread_mutex_lock(&detector->stats_lock);
    detector->config.enabled = enabled;
    pthread_mutex_unlock(&detector->stats_lock);
}

bool invalid_free_is_enabled(invalid_free_detector_t* detector) {
    if (!detector || !detector->initialized) {
        return false;
    }
    
    pthread_mutex_lock(&detector->stats_lock);
    bool enabled = detector->config.enabled;
    pthread_mutex_unlock(&detector->stats_lock);
    
    return enabled;
}

void invalid_free_set_severity(invalid_free_detector_t* detector,
                               invalid_free_severity_t severity) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    pthread_mutex_lock(&detector->stats_lock);
    detector->config.severity = severity;
    pthread_mutex_unlock(&detector->stats_lock);
}

invalid_free_severity_t invalid_free_get_severity(invalid_free_detector_t* detector) {
    if (!detector || !detector->initialized) {
        return INVALID_FREE_SEVERITY_ERROR;
    }
    
    pthread_mutex_lock(&detector->stats_lock);
    invalid_free_severity_t severity = detector->config.severity;
    pthread_mutex_unlock(&detector->stats_lock);
    
    return severity;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char* invalid_free_type_to_string(invalid_free_type_t type) {
    switch (type) {
        case INVALID_FREE_TYPE_UNTRACKED:
            return "UNTRACKED";
        case INVALID_FREE_TYPE_ALREADY_FREED:
            return "ALREADY_FREED";
        default:
            return "UNKNOWN";
    }
}

const char* invalid_free_severity_to_string(invalid_free_severity_t severity) {
    switch (severity) {
        case INVALID_FREE_SEVERITY_WARNING:
            return "WARNING";
        case INVALID_FREE_SEVERITY_ERROR:
            return "ERROR";
        case INVALID_FREE_SEVERITY_FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

char* invalid_free_format_violation(const invalid_free_violation_t* violation) {
    if (!violation) {
        return NULL;
    }
    
    /* Estimate buffer size */
    const size_t buffer_size = 4096;
    char* buffer = malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }
    
    size_t offset = 0;
    size_t remaining = buffer_size;
    int written;
    
    /* Header */
    written = snprintf(buffer + offset, remaining,
        "========== INVALID FREE DETECTED ==========\n"
        "Address: %p\n"
        "Type: %s\n",
        violation->address, invalid_free_type_to_string(violation->type));
    if (written > 0 && (size_t)written < remaining) {
        offset += (size_t)written;
        remaining -= (size_t)written;
    }
    
    /* Context */
    if (violation->context) {
        written = snprintf(buffer + offset, remaining,
            "Context: %s\n", violation->context);
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    
    written = snprintf(buffer + offset, remaining, "\n");
    if (written > 0 && (size_t)written < remaining) {
        offset += (size_t)written;
        remaining -= (size_t)written;
    }
    
    /* Free call info */
    written = snprintf(buffer + offset, remaining,
        "--- Invalid Free Location ---\n");
    if (written > 0 && (size_t)written < remaining) {
        offset += (size_t)written;
        remaining -= (size_t)written;
    }
    
    if (violation->free_file) {
        written = snprintf(buffer + offset, remaining,
            "Location: %s:%d\n",
            violation->free_file, violation->free_line);
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    
    if (violation->free_frame_count > 0) {
        written = snprintf(buffer + offset, remaining, "Backtrace:\n");
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
        
        char** symbols = backtrace_to_symbols_raw(violation->free_frames,
                                                  violation->free_frame_count);
        for (int i = 0; i < violation->free_frame_count && remaining > 0; i++) {
            const char* sym = (symbols && symbols[i]) ? symbols[i] : "???";
            written = snprintf(buffer + offset, remaining,
                "  #%d %s\n", i, sym);
            if (written > 0 && (size_t)written < remaining) {
                offset += (size_t)written;
                remaining -= (size_t)written;
            }
        }
        free(symbols);
    }
    
    written = snprintf(buffer + offset, remaining,
        "===========================================\n");
    if (written > 0 && (size_t)written < remaining) {
        offset += (size_t)written;
        remaining -= (size_t)written;
    }
    
    (void)offset; /* Suppress unused warning */
    
    return buffer;
}

void invalid_free_print_violation(const invalid_free_violation_t* violation) {
    if (!violation) {
        return;
    }
    
    char* formatted = invalid_free_format_violation(violation);
    if (formatted) {
        fprintf(stderr, "%s", formatted);
        free(formatted);
    }
}
