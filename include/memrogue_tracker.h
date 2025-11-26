#ifndef MEMROGUE_TRACKER_H
#define MEMROGUE_TRACKER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "memrogue_allocation_record.h"
#include "memrogue_hash_table.h"
#include "memrogue_backtrace.h"

// ============================================================================
// Tracker Statistics
// ============================================================================

/**
 * Structure to hold allocation statistics.
 */
typedef struct {
    uint64_t total_allocations;      // Total number of allocations since start
    uint64_t total_deallocations;    // Total number of deallocations since start
    uint64_t active_allocations;     // Current number of active allocations
    uint64_t peak_allocations;       // Peak number of simultaneous allocations
    
    uint64_t total_bytes_allocated;  // Total bytes allocated since start
    uint64_t total_bytes_freed;      // Total bytes freed since start
    uint64_t active_bytes;           // Current bytes in use
    uint64_t peak_bytes;             // Peak bytes in use simultaneously
    
    uint64_t failed_allocations;     // Number of failed allocation tracks (internal errors)
    uint64_t double_frees;           // Reserved for future use: double-free detection (not currently implemented)
    uint64_t unknown_frees;          // Number of frees on untracked pointers
} tracker_stats_t;

// ============================================================================
// Tracker Configuration
// ============================================================================

/**
 * Configuration options for the memory tracker.
 */
typedef struct {
    size_t hash_table_size;          // Initial hash table size (default: 4096)
    bool capture_backtraces;          // Whether to capture backtraces (default: true)
    int backtrace_skip_frames;       // Frames to skip in backtrace (default: 2)
    bool use_frame_filter;           // Use pattern-based frame filtering (default: true)
    frame_filter_t frame_filter;     // Frame filter configuration
} tracker_config_t;

// ============================================================================
// Memory Tracker Structure
// ============================================================================

/**
 * The main memory tracker structure.
 * 
 * Tracks all memory allocations with optional backtrace capture.
 * Thread-safe for concurrent use.
 */
typedef struct {
    hash_table_t* allocations;       // Hash table of active allocations
    tracker_stats_t stats;           // Allocation statistics
    tracker_config_t config;         // Configuration options
    pthread_mutex_t stats_lock;      // Lock for statistics updates
    bool initialized;                // Whether tracker is properly initialized
} memory_tracker_t;

// ============================================================================
// Tracker Lifecycle Functions
// ============================================================================

/**
 * Initialize tracker configuration with default values.
 * 
 * @param config The configuration structure to initialize
 */
void tracker_config_init(tracker_config_t* config);

/**
 * Create a new memory tracker with default configuration.
 * 
 * @return A newly allocated tracker, or NULL on failure
 */
memory_tracker_t* tracker_create(void);

/**
 * Create a new memory tracker with custom configuration.
 * 
 * @param config The configuration to use
 * @return A newly allocated tracker, or NULL on failure
 */
memory_tracker_t* tracker_create_with_config(const tracker_config_t* config);

/**
 * Destroy a memory tracker and free all resources.
 * 
 * This will also free any remaining tracked allocations (records only,
 * not the actual memory - that would indicate a leak).
 * 
 * @param tracker The tracker to destroy (may be NULL)
 */
void tracker_destroy(memory_tracker_t* tracker);

// ============================================================================
// Allocation Tracking Functions
// ============================================================================

/**
 * Track a new memory allocation.
 * 
 * Records the allocation with optional backtrace capture.
 * Updates statistics accordingly.
 * 
 * @param tracker The memory tracker
 * @param ptr The allocated pointer
 * @param size The allocation size in bytes
 * @param file Source file name (may be NULL)
 * @param line Source line number (0 if unknown)
 * @return true on success, false on failure
 */
bool track_allocation(memory_tracker_t* tracker, void* ptr, size_t size,
                      const char* file, int line);

/**
 * Track a memory deallocation (free).
 * 
 * Removes the allocation record and updates statistics.
 * Detects frees of untracked pointers.
 * 
 * @param tracker The memory tracker
 * @param ptr The pointer being freed
 * @return true if the allocation was found and removed,
 *         false if the pointer was not tracked (potential error)
 */
bool track_deallocation(memory_tracker_t* tracker, void* ptr);

/**
 * Look up an allocation by pointer.
 * 
 * @param tracker The memory tracker
 * @param ptr The pointer to look up
 * @return The allocation info, or NULL if not found.
 *         The returned pointer is valid until the allocation is freed.
 */
allocation_info_t* lookup_allocation(memory_tracker_t* tracker, void* ptr);

// ============================================================================
// Statistics Functions
// ============================================================================

/**
 * Get a copy of the current statistics.
 * 
 * @param tracker The memory tracker
 * @param out_stats Output structure to fill with current stats
 */
void tracker_get_stats(memory_tracker_t* tracker, tracker_stats_t* out_stats);

/**
 * Reset statistics counters to zero.
 * 
 * Note: This only resets counters, not active allocation tracking.
 * Active allocations and bytes are recalculated from current state.
 * 
 * @param tracker The memory tracker
 */
void tracker_reset_stats(memory_tracker_t* tracker);

/**
 * Get the number of currently active allocations.
 * 
 * @param tracker The memory tracker
 * @return Number of active allocations
 */
uint64_t tracker_active_count(memory_tracker_t* tracker);

/**
 * Get the number of bytes currently in use.
 * 
 * @param tracker The memory tracker
 * @return Number of active bytes
 */
uint64_t tracker_active_bytes(memory_tracker_t* tracker);

// ============================================================================
// Iteration Functions
// ============================================================================

/**
 * Callback function type for iterating over allocations.
 * 
 * @param info The allocation record
 * @param user_data User-provided context data
 * @return true to continue iteration, false to stop
 */
typedef bool (*tracker_iterate_fn)(const allocation_info_t* info, void* user_data);

/**
 * Iterate over all active allocations.
 * 
 * The callback is called for each allocation while holding the tracker lock.
 * Do not call tracker functions from within the callback.
 * 
 * @param tracker The memory tracker
 * @param callback Function to call for each allocation
 * @param user_data User context passed to callback
 */
void tracker_iterate(memory_tracker_t* tracker, tracker_iterate_fn callback, void* user_data);

#endif // MEMROGUE_TRACKER_H
