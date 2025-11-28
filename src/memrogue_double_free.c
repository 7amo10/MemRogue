/**
 * @file memrogue_double_free.c
 * @brief Implementation of double-free detection.
 *
 * Uses an LRU cache to track recently freed pointers. When a free is attempted,
 * the cache is checked. If found, a double-free violation is reported.
 *
 * Implementation Details:
 * - Hash table + doubly-linked list for O(1) LRU cache operations
 * - Per-bucket locking for minimal contention in multithreaded scenarios
 * - Copy-on-store for file names to avoid dangling pointers
 * - Timestamp-based ordering for accurate violation reports
 *
 * MEMRO-15: Double-Free Detection
 */

/* Ensure POSIX features are available (must be before any includes) */
#define _POSIX_C_SOURCE 200809L  /* For strdup and clock_gettime */

#include "memrogue_double_free.h"
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
 * Entry in the freed pointer cache.
 * 
 * Stored in both a hash table (for O(1) lookup) and a doubly-linked list
 * (for O(1) LRU eviction).
 */
typedef struct freed_entry {
    void* ptr;                                  /**< The freed pointer (key) */
    size_t size;                                /**< Size of original allocation */
    
    /* Allocation info (if recorded) */
    char* alloc_file;                           /**< Owned copy of alloc file */
    int alloc_line;                             /**< Allocation line number */
    uint64_t alloc_timestamp;                   /**< When allocated */
    void* alloc_frames[MEMROGUE_MAX_FRAMES];    /**< Allocation backtrace */
    int alloc_frame_count;                      /**< Number of alloc frames */
    
    /* Free info */
    char* free_file;                            /**< Owned copy of free file */
    int free_line;                              /**< Free line number */
    uint64_t free_timestamp;                    /**< When freed */
    void* free_frames[MEMROGUE_MAX_FRAMES];     /**< Free backtrace */
    int free_frame_count;                       /**< Number of free frames */
    
    /* Hash table chaining */
    struct freed_entry* hash_next;              /**< Next in hash bucket */
    
    /* LRU list (most recent at head) */
    struct freed_entry* lru_prev;               /**< Previous in LRU list */
    struct freed_entry* lru_next;               /**< Next in LRU list */
} freed_entry_t;

/**
 * Allocation record for tracking active allocations.
 * 
 * Simpler than freed_entry since we only need allocation info.
 */
typedef struct alloc_entry {
    void* ptr;                                  /**< The allocated pointer */
    size_t size;                                /**< Allocation size */
    char* file;                                 /**< Owned copy of file name */
    int line;                                   /**< Line number */
    uint64_t timestamp;                         /**< When allocated */
    void* frames[MEMROGUE_MAX_FRAMES];          /**< Backtrace frames */
    int frame_count;                            /**< Number of frames */
    struct alloc_entry* next;                   /**< Next in hash bucket */
} alloc_entry_t;

/**
 * Hash bucket for freed pointer cache.
 */
typedef struct {
    freed_entry_t* head;                        /**< Head of bucket chain */
} freed_bucket_t;

/**
 * Hash bucket for allocation records.
 */
typedef struct {
    alloc_entry_t* head;                        /**< Head of bucket chain */
} alloc_bucket_t;

/**
 * Internal double-free detector structure.
 */
struct double_free_detector_internal {
    /* Configuration */
    double_free_config_t config;
    
    /* Freed pointer cache (hash table + LRU list) */
    freed_bucket_t* freed_buckets;              /**< Hash buckets for freed pointers */
    size_t freed_bucket_count;                  /**< Number of buckets */
    freed_entry_t* lru_head;                    /**< Most recently freed (head) */
    freed_entry_t* lru_tail;                    /**< Least recently freed (tail) */
    size_t freed_count;                         /**< Current number of cached freed ptrs */
    pthread_mutex_t freed_lock;                 /**< Lock for freed cache */
    
    /* Allocation records (hash table) */
    alloc_bucket_t* alloc_buckets;              /**< Hash buckets for allocations */
    size_t alloc_bucket_count;                  /**< Number of buckets */
    pthread_mutex_t alloc_lock;                 /**< Lock for allocation records */
    
    /* Violation handling */
    double_free_callback_t callback;            /**< User callback */
    void* callback_user_data;                   /**< Callback context */
    double_free_violation_t last_violation;     /**< Most recent violation */
    bool has_violation;                         /**< Whether last_violation is valid */
    pthread_mutex_t violation_lock;             /**< Lock for violation data */
    
    /* Statistics */
    double_free_stats_t stats;
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
 * Freed Entry Management
 * ============================================================================ */

/**
 * Create a new freed entry.
 */
static freed_entry_t* freed_entry_create(void* ptr, size_t size,
                                          const char* free_file, int free_line,
                                          uint64_t timestamp) {
    freed_entry_t* entry = calloc(1, sizeof(freed_entry_t));
    if (!entry) {
        return NULL;
    }
    
    entry->ptr = ptr;
    entry->size = size;
    entry->free_file = safe_strdup(free_file);
    entry->free_line = free_line;
    entry->free_timestamp = timestamp;
    
    return entry;
}

/**
 * Destroy a freed entry.
 */
static void freed_entry_destroy(freed_entry_t* entry) {
    if (!entry) {
        return;
    }
    
    free(entry->alloc_file);
    free(entry->free_file);
    free(entry);
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
 * LRU List Operations
 * ============================================================================ */

/**
 * Add entry to head of LRU list.
 * Caller must hold freed_lock.
 */
static void lru_add_head(double_free_detector_t* detector, freed_entry_t* entry) {
    entry->lru_prev = NULL;
    entry->lru_next = detector->lru_head;
    
    if (detector->lru_head) {
        detector->lru_head->lru_prev = entry;
    }
    detector->lru_head = entry;
    
    if (!detector->lru_tail) {
        detector->lru_tail = entry;
    }
}

/**
 * Remove entry from LRU list.
 * Caller must hold freed_lock.
 */
static void lru_remove(double_free_detector_t* detector, freed_entry_t* entry) {
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        detector->lru_head = entry->lru_next;
    }
    
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        detector->lru_tail = entry->lru_prev;
    }
    
    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

/**
 * Remove and return the tail (oldest) entry from LRU list.
 * Caller must hold freed_lock.
 */
static freed_entry_t* lru_pop_tail(double_free_detector_t* detector) {
    freed_entry_t* entry = detector->lru_tail;
    if (entry) {
        lru_remove(detector, entry);
    }
    return entry;
}

/* ============================================================================
 * Hash Table Operations
 * ============================================================================ */

/**
 * Find a freed entry in the hash table.
 * Caller must hold freed_lock.
 */
static freed_entry_t* freed_find(double_free_detector_t* detector, void* ptr) {
    size_t bucket = hash_ptr(ptr, detector->freed_bucket_count);
    freed_entry_t* entry = detector->freed_buckets[bucket].head;
    
    while (entry) {
        if (entry->ptr == ptr) {
            return entry;
        }
        entry = entry->hash_next;
    }
    
    return NULL;
}

/**
 * Add a freed entry to the hash table.
 * Caller must hold freed_lock.
 */
static void freed_add(double_free_detector_t* detector, freed_entry_t* entry) {
    size_t bucket = hash_ptr(entry->ptr, detector->freed_bucket_count);
    entry->hash_next = detector->freed_buckets[bucket].head;
    detector->freed_buckets[bucket].head = entry;
}

/**
 * Remove a freed entry from the hash table.
 * Caller must hold freed_lock.
 */
static void freed_remove_from_hash(double_free_detector_t* detector,
                                    freed_entry_t* entry) {
    size_t bucket = hash_ptr(entry->ptr, detector->freed_bucket_count);
    freed_entry_t** pp = &detector->freed_buckets[bucket].head;
    
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            entry->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/**
 * Find an allocation entry in the hash table.
 * Caller must hold alloc_lock.
 * 
 * Note: Currently unused but may be useful for future lookup operations.
 */
__attribute__((unused))
static alloc_entry_t* alloc_find(double_free_detector_t* detector, void* ptr) {
    size_t bucket = hash_ptr(ptr, detector->alloc_bucket_count);
    alloc_entry_t* entry = detector->alloc_buckets[bucket].head;
    
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
static void alloc_add(double_free_detector_t* detector, alloc_entry_t* entry) {
    size_t bucket = hash_ptr(entry->ptr, detector->alloc_bucket_count);
    entry->next = detector->alloc_buckets[bucket].head;
    detector->alloc_buckets[bucket].head = entry;
}

/**
 * Remove and return an allocation entry from the hash table.
 * Caller must hold alloc_lock.
 */
static alloc_entry_t* alloc_remove(double_free_detector_t* detector, void* ptr) {
    size_t bucket = hash_ptr(ptr, detector->alloc_bucket_count);
    alloc_entry_t** pp = &detector->alloc_buckets[bucket].head;
    
    while (*pp) {
        if ((*pp)->ptr == ptr) {
            alloc_entry_t* entry = *pp;
            *pp = entry->next;
            entry->next = NULL;
            return entry;
        }
        pp = &(*pp)->next;
    }
    
    return NULL;
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

void double_free_config_init(double_free_config_t* config) {
    if (!config) {
        return;
    }
    
    config->enabled = true;
    config->abort_on_error = false;
    config->print_on_error = true;
    config->cache_size = DOUBLE_FREE_DEFAULT_CACHE_SIZE;
    config->backtrace_skip_frames = 2;
}

/* ============================================================================
 * Detector Lifecycle
 * ============================================================================ */

double_free_detector_t* double_free_detector_create(void) {
    double_free_config_t config;
    double_free_config_init(&config);
    return double_free_detector_create_with_config(&config);
}

double_free_detector_t* double_free_detector_create_with_config(
    const double_free_config_t* config) {
    if (!config) {
        return NULL;
    }
    
    double_free_detector_t* detector = calloc(1, sizeof(double_free_detector_t));
    if (!detector) {
        return NULL;
    }
    
    /* Copy configuration */
    detector->config = *config;
    
    /* Calculate bucket counts (use prime-ish numbers for better distribution) */
    detector->freed_bucket_count = (config->cache_size / 4) | 1; /* Ensure odd */
    if (detector->freed_bucket_count < 17) {
        detector->freed_bucket_count = 17;
    }
    detector->alloc_bucket_count = detector->freed_bucket_count;
    
    /* Allocate hash tables */
    detector->freed_buckets = calloc(detector->freed_bucket_count,
                                      sizeof(freed_bucket_t));
    if (!detector->freed_buckets) {
        goto fail;
    }
    
    detector->alloc_buckets = calloc(detector->alloc_bucket_count,
                                      sizeof(alloc_bucket_t));
    if (!detector->alloc_buckets) {
        goto fail;
    }
    
    /* Initialize locks */
    if (pthread_mutex_init(&detector->freed_lock, NULL) != 0) {
        goto fail;
    }
    
    if (pthread_mutex_init(&detector->alloc_lock, NULL) != 0) {
        pthread_mutex_destroy(&detector->freed_lock);
        goto fail;
    }
    
    if (pthread_mutex_init(&detector->violation_lock, NULL) != 0) {
        pthread_mutex_destroy(&detector->freed_lock);
        pthread_mutex_destroy(&detector->alloc_lock);
        goto fail;
    }
    
    if (pthread_mutex_init(&detector->stats_lock, NULL) != 0) {
        pthread_mutex_destroy(&detector->freed_lock);
        pthread_mutex_destroy(&detector->alloc_lock);
        pthread_mutex_destroy(&detector->violation_lock);
        goto fail;
    }
    
    detector->initialized = true;
    return detector;
    
fail:
    free(detector->freed_buckets);
    free(detector->alloc_buckets);
    free(detector);
    return NULL;
}

void double_free_detector_destroy(double_free_detector_t* detector) {
    if (!detector) {
        return;
    }
    
    /* Free all freed entries */
    for (size_t i = 0; i < detector->freed_bucket_count; i++) {
        freed_entry_t* entry = detector->freed_buckets[i].head;
        while (entry) {
            freed_entry_t* next = entry->hash_next;
            freed_entry_destroy(entry);
            entry = next;
        }
    }
    
    /* Free all allocation entries */
    for (size_t i = 0; i < detector->alloc_bucket_count; i++) {
        alloc_entry_t* entry = detector->alloc_buckets[i].head;
        while (entry) {
            alloc_entry_t* next = entry->next;
            alloc_entry_destroy(entry);
            entry = next;
        }
    }
    
    /* Free hash tables */
    free(detector->freed_buckets);
    free(detector->alloc_buckets);
    
    /* Free owned strings in last violation */
    /* Note: file pointers in violation are const char*, not owned */
    
    /* Destroy locks */
    pthread_mutex_destroy(&detector->freed_lock);
    pthread_mutex_destroy(&detector->alloc_lock);
    pthread_mutex_destroy(&detector->violation_lock);
    pthread_mutex_destroy(&detector->stats_lock);
    
    detector->initialized = false;
    free(detector);
}

/* ============================================================================
 * Detection API
 * ============================================================================ */

void double_free_record_alloc(double_free_detector_t* detector, void* ptr,
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
    
    /* Remove any existing entry for this pointer (shouldn't happen normally) */
    alloc_entry_t* old = alloc_remove(detector, ptr);
    if (old) {
        alloc_entry_destroy(old);
    }
    
    alloc_add(detector, entry);
    
    pthread_mutex_unlock(&detector->alloc_lock);
    
    /* Also remove from freed cache if present (reuse of address) */
    pthread_mutex_lock(&detector->freed_lock);
    freed_entry_t* freed = freed_find(detector, ptr);
    if (freed) {
        freed_remove_from_hash(detector, freed);
        lru_remove(detector, freed);
        detector->freed_count--;
        freed_entry_destroy(freed);
    }
    pthread_mutex_unlock(&detector->freed_lock);
    
    /* Update stats */
    pthread_mutex_lock(&detector->stats_lock);
    detector->stats.allocs_recorded++;
    pthread_mutex_unlock(&detector->stats_lock);
}

/**
 * Build a violation report from the current state.
 * Caller holds appropriate locks.
 */
static void build_violation(double_free_detector_t* detector,
                             freed_entry_t* prev_free,
                             void* ptr, const char* file, int line,
                             uint64_t timestamp,
                             void* frames[], int frame_count) {
    double_free_violation_t* v = &detector->last_violation;
    
    /* Clear previous violation */
    memset(v, 0, sizeof(*v));
    
    v->address = ptr;
    v->size = prev_free->size;
    
    /* Copy allocation info */
    v->alloc_file = prev_free->alloc_file;
    v->alloc_line = prev_free->alloc_line;
    v->alloc_timestamp = prev_free->alloc_timestamp;
    memcpy(v->alloc_frames, prev_free->alloc_frames,
           sizeof(void*) * (size_t)prev_free->alloc_frame_count);
    v->alloc_frame_count = prev_free->alloc_frame_count;
    
    /* Copy first free info */
    v->first_free_file = prev_free->free_file;
    v->first_free_line = prev_free->free_line;
    v->first_free_timestamp = prev_free->free_timestamp;
    memcpy(v->first_free_frames, prev_free->free_frames,
           sizeof(void*) * (size_t)prev_free->free_frame_count);
    v->first_free_frame_count = prev_free->free_frame_count;
    
    /* Set second free info */
    v->second_free_file = file;
    v->second_free_line = line;
    v->second_free_timestamp = timestamp;
    memcpy(v->second_free_frames, frames, sizeof(void*) * (size_t)frame_count);
    v->second_free_frame_count = frame_count;
    
    detector->has_violation = true;
}

bool double_free_check_and_record(double_free_detector_t* detector, void* ptr,
                                   const char* file, int line) {
    if (!detector || !detector->initialized || !ptr) {
        return true; /* No detection = valid free */
    }
    
    if (!detector->config.enabled) {
        return true;
    }
    
    uint64_t timestamp = get_timestamp_us();
    
    /* Capture backtrace for this free */
    void* frames[MEMROGUE_MAX_FRAMES];
    int frame_count = capture_backtrace_raw(frames, MEMROGUE_MAX_FRAMES,
                                         detector->config.backtrace_skip_frames);
    
    /* Variables for callback invocation outside locks */
    bool is_double_free = false;
    (void)is_double_free; /* Used for clarity, suppress warning */
    double_free_callback_t callback = NULL;
    void* callback_data = NULL;
    double_free_violation_t violation_copy;
    
    /* Check if already freed */
    pthread_mutex_lock(&detector->freed_lock);
    
    freed_entry_t* prev_free = freed_find(detector, ptr);
    if (prev_free) {
        /* Double-free detected! */
        is_double_free = true;
        
        /* Build violation report */
        pthread_mutex_lock(&detector->violation_lock);
        build_violation(detector, prev_free, ptr, file, line, timestamp,
                       frames, frame_count);
        
        /* Copy for callback (to call outside lock) */
        violation_copy = detector->last_violation;
        callback = detector->callback;
        callback_data = detector->callback_user_data;
        pthread_mutex_unlock(&detector->violation_lock);
        
        /* Update stats */
        pthread_mutex_lock(&detector->stats_lock);
        detector->stats.double_frees_detected++;
        pthread_mutex_unlock(&detector->stats_lock);
        
        pthread_mutex_unlock(&detector->freed_lock);
        
        /* Call callback outside of locks to prevent deadlock */
        if (callback) {
            callback(&violation_copy, callback_data);
        }
        
        /* Print if configured */
        if (detector->config.print_on_error) {
            double_free_print_violation(&violation_copy);
        }
        
        /* Abort if configured */
        if (detector->config.abort_on_error) {
            abort();
        }
        
        return false;
    }
    
    /* Not a double-free - record this free */
    
    /* Get allocation info if available */
    size_t size = 0;
    char* alloc_file = NULL;
    int alloc_line = 0;
    uint64_t alloc_timestamp = 0;
    void* alloc_frames[MEMROGUE_MAX_FRAMES] = {0};
    int alloc_frame_count = 0;
    
    pthread_mutex_lock(&detector->alloc_lock);
    alloc_entry_t* alloc = alloc_remove(detector, ptr);
    if (alloc) {
        size = alloc->size;
        alloc_file = alloc->file;  /* Take ownership */
        alloc->file = NULL;        /* Prevent double-free */
        alloc_line = alloc->line;
        alloc_timestamp = alloc->timestamp;
        memcpy(alloc_frames, alloc->frames, sizeof(void*) * (size_t)alloc->frame_count);
        alloc_frame_count = alloc->frame_count;
        alloc_entry_destroy(alloc);
    }
    pthread_mutex_unlock(&detector->alloc_lock);
    
    /* Create freed entry */
    freed_entry_t* entry = freed_entry_create(ptr, size, file, line, timestamp);
    if (entry) {
        /* Copy allocation info */
        entry->alloc_file = alloc_file;  /* Transfer ownership */
        alloc_file = NULL;
        entry->alloc_line = alloc_line;
        entry->alloc_timestamp = alloc_timestamp;
        memcpy(entry->alloc_frames, alloc_frames, sizeof(void*) * (size_t)alloc_frame_count);
        entry->alloc_frame_count = alloc_frame_count;
        
        /* Copy free backtrace */
        memcpy(entry->free_frames, frames, sizeof(void*) * (size_t)frame_count);
        entry->free_frame_count = frame_count;
        
        /* Evict if cache is full */
        if (detector->freed_count >= detector->config.cache_size) {
            freed_entry_t* evicted = lru_pop_tail(detector);
            if (evicted) {
                freed_remove_from_hash(detector, evicted);
                freed_entry_destroy(evicted);
                detector->freed_count--;
                
                pthread_mutex_lock(&detector->stats_lock);
                detector->stats.cache_evictions++;
                pthread_mutex_unlock(&detector->stats_lock);
            }
        }
        
        /* Add to cache */
        freed_add(detector, entry);
        lru_add_head(detector, entry);
        detector->freed_count++;
        
        pthread_mutex_lock(&detector->stats_lock);
        detector->stats.frees_recorded++;
        detector->stats.current_cache_entries = detector->freed_count;
        pthread_mutex_unlock(&detector->stats_lock);
    } else {
        /* Failed to create entry - free alloc_file if we took it */
        free(alloc_file);
    }
    
    pthread_mutex_unlock(&detector->freed_lock);
    
    return true;
}

void double_free_remove_record(double_free_detector_t* detector, void* ptr) {
    if (!detector || !detector->initialized || !ptr) {
        return;
    }
    
    /* Remove from allocation cache */
    pthread_mutex_lock(&detector->alloc_lock);
    alloc_entry_t* alloc = alloc_remove(detector, ptr);
    if (alloc) {
        alloc_entry_destroy(alloc);
    }
    pthread_mutex_unlock(&detector->alloc_lock);
    
    /* Remove from freed cache */
    pthread_mutex_lock(&detector->freed_lock);
    freed_entry_t* freed = freed_find(detector, ptr);
    if (freed) {
        freed_remove_from_hash(detector, freed);
        lru_remove(detector, freed);
        detector->freed_count--;
        freed_entry_destroy(freed);
    }
    pthread_mutex_unlock(&detector->freed_lock);
}

/* ============================================================================
 * Violation Handling
 * ============================================================================ */

void double_free_set_callback(double_free_detector_t* detector,
                               double_free_callback_t callback,
                               void* user_data) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    pthread_mutex_lock(&detector->violation_lock);
    detector->callback = callback;
    detector->callback_user_data = user_data;
    pthread_mutex_unlock(&detector->violation_lock);
}

bool double_free_get_last_violation(
    double_free_detector_t* detector,
    double_free_violation_t* out_violation) {
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

void double_free_get_stats(double_free_detector_t* detector,
                            double_free_stats_t* out_stats) {
    if (!detector || !detector->initialized || !out_stats) {
        if (out_stats) {
            memset(out_stats, 0, sizeof(*out_stats));
        }
        return;
    }
    
    pthread_mutex_lock(&detector->stats_lock);
    *out_stats = detector->stats;
    pthread_mutex_unlock(&detector->stats_lock);
}

void double_free_reset_stats(double_free_detector_t* detector) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    pthread_mutex_lock(&detector->stats_lock);
    memset(&detector->stats, 0, sizeof(detector->stats));
    pthread_mutex_unlock(&detector->stats_lock);
    
    /* Update current_cache_entries with proper locking */
    pthread_mutex_lock(&detector->freed_lock);
    size_t count = detector->freed_count;
    pthread_mutex_unlock(&detector->freed_lock);
    
    pthread_mutex_lock(&detector->stats_lock);
    detector->stats.current_cache_entries = count;
    pthread_mutex_unlock(&detector->stats_lock);
}

/* ============================================================================
 * Configuration Updates
 * ============================================================================ */

void double_free_set_enabled(double_free_detector_t* detector, bool enabled) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    /* Note: Not locking since config.enabled is a single bool and
       we just want eventual consistency, not strict ordering */
    detector->config.enabled = enabled;
}

bool double_free_is_enabled(double_free_detector_t* detector) {
    if (!detector || !detector->initialized) {
        return false;
    }
    
    return detector->config.enabled;
}

void double_free_set_abort_on_error(double_free_detector_t* detector,
                                     bool abort_on_error) {
    if (!detector || !detector->initialized) {
        return;
    }
    
    /* Lock to ensure thread-safe configuration update */
    pthread_mutex_lock(&detector->stats_lock);
    detector->config.abort_on_error = abort_on_error;
    pthread_mutex_unlock(&detector->stats_lock);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

char* double_free_format_violation(const double_free_violation_t* violation) {
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
        "========== DOUBLE-FREE DETECTED ==========\n"
        "Address: %p\n"
        "Size: %zu bytes\n\n",
        violation->address, violation->size);
    if (written > 0 && (size_t)written < remaining) {
        offset += (size_t)written;
        remaining -= (size_t)written;
    }
    
    /* Allocation info */
    if (violation->alloc_file || violation->alloc_frame_count > 0) {
        written = snprintf(buffer + offset, remaining,
            "--- Original Allocation ---\n");
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
        
        if (violation->alloc_file) {
            written = snprintf(buffer + offset, remaining,
                "Location: %s:%d\n",
                violation->alloc_file, violation->alloc_line);
            if (written > 0 && (size_t)written < remaining) {
                offset += (size_t)written;
                remaining -= (size_t)written;
            }
        }
        
        if (violation->alloc_frame_count > 0) {
            written = snprintf(buffer + offset, remaining, "Backtrace:\n");
            if (written > 0 && (size_t)written < remaining) {
                offset += (size_t)written;
                remaining -= (size_t)written;
            }
            
            char** symbols = backtrace_to_symbols_raw(violation->alloc_frames,
                                                   violation->alloc_frame_count);
            for (int i = 0; i < violation->alloc_frame_count && remaining > 0; i++) {
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
        
        written = snprintf(buffer + offset, remaining, "\n");
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    
    /* First free info */
    written = snprintf(buffer + offset, remaining,
        "--- First Free (valid) ---\n");
    if (written > 0 && (size_t)written < remaining) {
        offset += (size_t)written;
        remaining -= (size_t)written;
    }
    
    if (violation->first_free_file) {
        written = snprintf(buffer + offset, remaining,
            "Location: %s:%d\n",
            violation->first_free_file, violation->first_free_line);
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    
    if (violation->first_free_frame_count > 0) {
        written = snprintf(buffer + offset, remaining, "Backtrace:\n");
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
        
        char** symbols = backtrace_to_symbols_raw(violation->first_free_frames,
                                               violation->first_free_frame_count);
        for (int i = 0; i < violation->first_free_frame_count && remaining > 0; i++) {
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
    
    /* Second free info */
    written = snprintf(buffer + offset, remaining,
        "\n--- Second Free (INVALID) ---\n");
    if (written > 0 && (size_t)written < remaining) {
        offset += (size_t)written;
        remaining -= (size_t)written;
    }
    
    if (violation->second_free_file) {
        written = snprintf(buffer + offset, remaining,
            "Location: %s:%d\n",
            violation->second_free_file, violation->second_free_line);
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    
    if (violation->second_free_frame_count > 0) {
        written = snprintf(buffer + offset, remaining, "Backtrace:\n");
        if (written > 0 && (size_t)written < remaining) {
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
        
        char** symbols = backtrace_to_symbols_raw(violation->second_free_frames,
                                               violation->second_free_frame_count);
        for (int i = 0; i < violation->second_free_frame_count && remaining > 0; i++) {
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
        "==========================================\n");
    if (written > 0 && (size_t)written < remaining) {
        offset += (size_t)written;
        remaining -= (size_t)written;
    }
    
    (void)offset; /* Suppress unused warning */
    
    return buffer;
}

void double_free_print_violation(const double_free_violation_t* violation) {
    if (!violation) {
        return;
    }
    
    char* formatted = double_free_format_violation(violation);
    if (formatted) {
        fprintf(stderr, "%s", formatted);
        free(formatted);
    }
}
