#include "memrogue_tracker.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Configuration Defaults
// ============================================================================

#define DEFAULT_HASH_TABLE_SIZE 4096
#define DEFAULT_BACKTRACE_SKIP_FRAMES 2

void tracker_config_init(tracker_config_t* config) {
    if (!config) {
        return;
    }
    
    config->hash_table_size = DEFAULT_HASH_TABLE_SIZE;
    config->capture_backtraces = true;
    config->backtrace_skip_frames = DEFAULT_BACKTRACE_SKIP_FRAMES;
    config->use_frame_filter = true;
    
    // Initialize frame filter with defaults
    frame_filter_init(&config->frame_filter);
}

// ============================================================================
// Tracker Lifecycle
// ============================================================================

memory_tracker_t* tracker_create(void) {
    tracker_config_t config;
    tracker_config_init(&config);
    return tracker_create_with_config(&config);
}

memory_tracker_t* tracker_create_with_config(const tracker_config_t* config) {
    if (!config) {
        return NULL;
    }
    
    memory_tracker_t* tracker = calloc(1, sizeof(memory_tracker_t));
    if (!tracker) {
        return NULL;
    }
    
    // Create hash table
    tracker->allocations = hash_table_create(config->hash_table_size);
    if (!tracker->allocations) {
        free(tracker);
        return NULL;
    }
    
    // Copy configuration
    // Note: This is a shallow copy. Pattern strings in frame_filter.patterns[]
    // must remain valid for the lifetime of the tracker (typically string literals).
    tracker->config = *config;
    
    // Initialize statistics
    memset(&tracker->stats, 0, sizeof(tracker->stats));
    
    // Initialize locks
    if (pthread_mutex_init(&tracker->stats_lock, NULL) != 0) {
        hash_table_destroy(tracker->allocations);
        free(tracker);
        return NULL;
    }
    
    tracker->initialized = true;
    
    return tracker;
}

void tracker_destroy(memory_tracker_t* tracker) {
    if (!tracker) {
        return;
    }
    
    // Destroy hash table (this will also free all allocation_info_t records)
    if (tracker->allocations) {
        hash_table_destroy(tracker->allocations);
    }
    
    // Destroy locks
    pthread_mutex_destroy(&tracker->stats_lock);
    
    tracker->initialized = false;
    free(tracker);
}

// ============================================================================
// Allocation Tracking
// ============================================================================

bool track_allocation(memory_tracker_t* tracker, void* ptr, size_t size,
                      const char* file, int line) {
    if (!tracker || !tracker->initialized || !ptr) {
        return false;
    }
    
    bool success;
    
    if (tracker->config.capture_backtraces) {
        // Use backtrace capture with configured skip frames
        // Note: frame filtering is applied automatically during capture
        success = hash_table_insert_with_backtrace(
            tracker->allocations, ptr, size, file, line,
            tracker->config.backtrace_skip_frames);
    } else {
        // No backtrace capture
        success = hash_table_insert(tracker->allocations, ptr, size, file, line);
    }
    
    if (success) {
        // Update statistics
        pthread_mutex_lock(&tracker->stats_lock);
        tracker->stats.total_allocations++;
        tracker->stats.active_allocations++;
        tracker->stats.total_bytes_allocated += size;
        tracker->stats.active_bytes += size;
        
        // Update peaks
        if (tracker->stats.active_allocations > tracker->stats.peak_allocations) {
            tracker->stats.peak_allocations = tracker->stats.active_allocations;
        }
        if (tracker->stats.active_bytes > tracker->stats.peak_bytes) {
            tracker->stats.peak_bytes = tracker->stats.active_bytes;
        }
        pthread_mutex_unlock(&tracker->stats_lock);
    } else {
        pthread_mutex_lock(&tracker->stats_lock);
        tracker->stats.failed_allocations++;
        pthread_mutex_unlock(&tracker->stats_lock);
    }
    
    return success;
}

bool track_deallocation(memory_tracker_t* tracker, void* ptr) {
    if (!tracker || !tracker->initialized || !ptr) {
        return false;
    }
    
    // Look up the allocation to get size before removing
    // Note: There's a small race window between lookup and remove where another
    // thread could remove the same pointer. In that case, hash_table_remove
    // will return false and we won't update stats (which is correct behavior).
    allocation_info_t* info = hash_table_lookup(tracker->allocations, ptr);
    
    if (!info) {
        // Unknown free - pointer was never tracked
        pthread_mutex_lock(&tracker->stats_lock);
        tracker->stats.unknown_frees++;
        pthread_mutex_unlock(&tracker->stats_lock);
        
        return false;
    }
    
    size_t size = info->size;
    
    // Remove from hash table
    bool removed = hash_table_remove(tracker->allocations, ptr);
    
    if (removed) {
        // Update statistics
        pthread_mutex_lock(&tracker->stats_lock);
        tracker->stats.total_deallocations++;
        tracker->stats.active_allocations--;
        tracker->stats.total_bytes_freed += size;
        tracker->stats.active_bytes -= size;
        pthread_mutex_unlock(&tracker->stats_lock);
    }
    // If removed is false, another thread beat us to it - no action needed
    
    return removed;
}

allocation_info_t* lookup_allocation(memory_tracker_t* tracker, void* ptr) {
    if (!tracker || !tracker->initialized || !ptr) {
        return NULL;
    }
    
    return hash_table_lookup(tracker->allocations, ptr);
}

// ============================================================================
// Statistics
// ============================================================================

void tracker_get_stats(memory_tracker_t* tracker, tracker_stats_t* out_stats) {
    if (!tracker || !tracker->initialized || !out_stats) {
        if (out_stats) {
            memset(out_stats, 0, sizeof(*out_stats));
        }
        return;
    }
    
    pthread_mutex_lock(&tracker->stats_lock);
    *out_stats = tracker->stats;
    pthread_mutex_unlock(&tracker->stats_lock);
}

void tracker_reset_stats(memory_tracker_t* tracker) {
    if (!tracker || !tracker->initialized) {
        return;
    }
    
    pthread_mutex_lock(&tracker->stats_lock);
    
    // Keep active counts, reset cumulative counters
    uint64_t active_allocs = tracker->stats.active_allocations;
    uint64_t active_bytes = tracker->stats.active_bytes;
    
    memset(&tracker->stats, 0, sizeof(tracker->stats));
    
    // Restore active counts
    tracker->stats.active_allocations = active_allocs;
    tracker->stats.active_bytes = active_bytes;
    tracker->stats.peak_allocations = active_allocs;
    tracker->stats.peak_bytes = active_bytes;
    
    pthread_mutex_unlock(&tracker->stats_lock);
}

uint64_t tracker_active_count(memory_tracker_t* tracker) {
    if (!tracker || !tracker->initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&tracker->stats_lock);
    uint64_t count = tracker->stats.active_allocations;
    pthread_mutex_unlock(&tracker->stats_lock);
    
    return count;
}

uint64_t tracker_active_bytes(memory_tracker_t* tracker) {
    if (!tracker || !tracker->initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&tracker->stats_lock);
    uint64_t bytes = tracker->stats.active_bytes;
    pthread_mutex_unlock(&tracker->stats_lock);
    
    return bytes;
}

// ============================================================================
// Iteration
// ============================================================================

void tracker_iterate(memory_tracker_t* tracker, tracker_iterate_fn callback, void* user_data) {
    if (!tracker || !tracker->initialized || !callback) {
        return;
    }
    
    // Use the hash table's iteration interface for proper encapsulation
    hash_table_iterate(tracker->allocations, callback, user_data);
}
