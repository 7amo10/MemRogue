#include "memrogue_tracker.h"
#include "memrogue_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

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

void tracker_config_from_global(tracker_config_t* config) {
    if (!config) {
        return;
    }
    
    // Start with defaults
    tracker_config_init(config);
    
    // Load global configuration if not already loaded
    const memrogue_config_t* global_config = config_get();
    if (!global_config) {
        return;
    }
    
    // Apply global config settings to tracker config
    config->capture_backtraces = global_config->backtrace_enabled;
    
    // Note: max_backtrace_depth is handled at capture time, not in frame_filter
    // The frame_filter.skip_count could be adjusted if needed, but max_depth
    // is applied during backtrace_capture() itself
}

// ============================================================================
// Tracker Lifecycle
// ============================================================================

memory_tracker_t* tracker_create(void) {
    tracker_config_t config;
    tracker_config_init(&config);
    return tracker_create_with_config(&config);
}

memory_tracker_t* tracker_create_from_global_config(void) {
    tracker_config_t config;
    tracker_config_from_global(&config);
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
    
    /* Initialize sampling fields if not set (MEMRO-21) */
    if (out_stats->sample_rate == 0) {
        out_stats->sample_rate = 100; /* Assume 100% if not set */
    }
    
    /* Set sampled_allocations to total if not tracked separately */
    if (out_stats->sampled_allocations == 0 && out_stats->total_allocations > 0) {
        out_stats->sampled_allocations = out_stats->total_allocations;
    }
}

void tracker_get_extrapolated_stats(memory_tracker_t* tracker, 
                                     tracker_stats_t* out_stats,
                                     int sample_rate) {
    if (!tracker || !tracker->initialized || !out_stats) {
        if (out_stats) {
            memset(out_stats, 0, sizeof(*out_stats));
        }
        return;
    }
    
    /* Clamp sample_rate to valid range */
    if (sample_rate < 1) sample_rate = 1;
    if (sample_rate > 100) sample_rate = 100;
    
    /* Get the raw stats */
    tracker_get_stats(tracker, out_stats);
    
    /* Store sample rate */
    out_stats->sample_rate = sample_rate;
    
    /* Calculate extrapolation factor
     * If sample_rate is 10%, we multiply observed values by 10 to estimate totals
     * factor = 100 / sample_rate
     *
     * Note on statistical limitations: The extrapolation assumes allocations and
     * deallocations are evenly distributed. Since only sampled allocations are
     * tracked, but deallocations only apply to the sampled set, the extrapolated
     * active counts may have higher variance than extrapolated totals. For more
     * accurate active allocation estimates, use higher sample rates.
     */
    double factor = 100.0 / (double)sample_rate;
    
    /* For 100% sampling, no extrapolation needed */
    if (sample_rate >= 100) {
        out_stats->estimated_total_allocations = out_stats->total_allocations;
        out_stats->estimated_total_bytes = out_stats->total_bytes_allocated;
        out_stats->estimated_active_allocations = out_stats->active_allocations;
        out_stats->estimated_active_bytes = out_stats->active_bytes;
        return;
    }
    
    /* Extrapolate estimates from sampled data */
    out_stats->estimated_total_allocations = 
        (uint64_t)((double)out_stats->total_allocations * factor);
    out_stats->estimated_total_bytes = 
        (uint64_t)((double)out_stats->total_bytes_allocated * factor);
    out_stats->estimated_active_allocations = 
        (uint64_t)((double)out_stats->active_allocations * factor);
    out_stats->estimated_active_bytes = 
        (uint64_t)((double)out_stats->active_bytes * factor);
    
    /* Record sampled allocations as the actual tracked count */
    out_stats->sampled_allocations = out_stats->total_allocations;
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

double tracker_average_allocation_size(memory_tracker_t* tracker) {
    if (!tracker || !tracker->initialized) {
        return 0.0;
    }
    
    pthread_mutex_lock(&tracker->stats_lock);
    uint64_t total_allocs = tracker->stats.total_allocations;
    uint64_t total_bytes = tracker->stats.total_bytes_allocated;
    pthread_mutex_unlock(&tracker->stats_lock);
    
    if (total_allocs == 0) {
        return 0.0;
    }
    
    return (double)total_bytes / (double)total_allocs;
}

char* tracker_format_stats(memory_tracker_t* tracker) {
    if (!tracker || !tracker->initialized) {
        return NULL;
    }
    
    /* Get extrapolated stats using current sample rate */
    tracker_stats_t stats;
    int sample_rate = config_get_sample_rate();
    tracker_get_extrapolated_stats(tracker, &stats, sample_rate);
    
    /* Calculate average from the snapshot to ensure consistency */
    double avg_size = (stats.total_allocations == 0) ? 0.0 
        : (double)stats.total_bytes_allocated / (double)stats.total_allocations;
    
    /* Calculate required buffer size and allocate
     * Using a larger buffer size to accommodate sampling info */
    const size_t buffer_size = 2048;
    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }
    
    int written;
    
    /* Format differently based on whether sampling is active */
    if (sample_rate < 100) {
        written = snprintf(buffer, buffer_size,
            "=== Memory Tracker Statistics ===\n"
            "Sampling:\n"
            "  Sample rate:     %d%%\n"
            "  Sampled allocs:  %" PRIu64 "\n"
            "  Skipped allocs:  %" PRIu64 "\n"
            "Allocations (sampled):\n"
            "  Total:    %" PRIu64 "\n"
            "  Active:   %" PRIu64 "\n"
            "  Peak:     %" PRIu64 "\n"
            "Allocations (estimated):\n"
            "  Total:    ~%" PRIu64 "\n"
            "  Active:   ~%" PRIu64 "\n"
            "Deallocations:\n"
            "  Total:    %" PRIu64 "\n"
            "Memory (sampled):\n"
            "  Total allocated: %" PRIu64 " bytes\n"
            "  Total freed:     %" PRIu64 " bytes\n"
            "  Active:          %" PRIu64 " bytes\n"
            "  Peak:            %" PRIu64 " bytes\n"
            "  Average size:    %.2f bytes\n"
            "Memory (estimated):\n"
            "  Total allocated: ~%" PRIu64 " bytes\n"
            "  Active:          ~%" PRIu64 " bytes\n"
            "Errors:\n"
            "  Failed allocs:   %" PRIu64 "\n"
            "  Unknown frees:   %" PRIu64 "\n"
            "=================================",
            stats.sample_rate,
            stats.sampled_allocations,
            stats.skipped_allocations,
            stats.total_allocations,
            stats.active_allocations,
            stats.peak_allocations,
            stats.estimated_total_allocations,
            stats.estimated_active_allocations,
            stats.total_deallocations,
            stats.total_bytes_allocated,
            stats.total_bytes_freed,
            stats.active_bytes,
            stats.peak_bytes,
            avg_size,
            stats.estimated_total_bytes,
            stats.estimated_active_bytes,
            stats.failed_allocations,
            stats.unknown_frees);
    } else {
        /* 100% sampling - simpler format without estimates */
        written = snprintf(buffer, buffer_size,
            "=== Memory Tracker Statistics ===\n"
            "Allocations:\n"
            "  Total:    %" PRIu64 "\n"
            "  Active:   %" PRIu64 "\n"
            "  Peak:     %" PRIu64 "\n"
            "Deallocations:\n"
            "  Total:    %" PRIu64 "\n"
            "Memory:\n"
            "  Total allocated: %" PRIu64 " bytes\n"
            "  Total freed:     %" PRIu64 " bytes\n"
            "  Active:          %" PRIu64 " bytes\n"
            "  Peak:            %" PRIu64 " bytes\n"
            "  Average size:    %.2f bytes\n"
            "Errors:\n"
            "  Failed allocs:   %" PRIu64 "\n"
            "  Unknown frees:   %" PRIu64 "\n"
            "=================================",
            stats.total_allocations,
            stats.active_allocations,
            stats.peak_allocations,
            stats.total_deallocations,
            stats.total_bytes_allocated,
            stats.total_bytes_freed,
            stats.active_bytes,
            stats.peak_bytes,
            avg_size,
            stats.failed_allocations,
            stats.unknown_frees);
    }
    
    if (written < 0 || (size_t)written >= buffer_size) {
        /* snprintf failed or truncated */
        free(buffer);
        return NULL;
    }
    
    return buffer;
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
