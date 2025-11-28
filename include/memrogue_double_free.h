/**
 * @file memrogue_double_free.h
 * @brief Double-free detection module for MemRogue memory debugger.
 *
 * This module detects double-free errors by maintaining a cache of recently
 * freed pointers along with their deallocation backtraces. When a pointer
 * is freed, the cache is checked to see if it was already freed.
 *
 * Features:
 * - Detects double-free attempts
 * - Captures backtrace of both free calls
 * - Reports violations with detailed context
 * - Configurable abort on detection
 * - Thread-safe operation
 * - LRU cache with configurable size
 *
 * MEMRO-15: Double-Free Detection
 */

#ifndef MEMROGUE_DOUBLE_FREE_H
#define MEMROGUE_DOUBLE_FREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memrogue_allocation_record.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * Default size of the freed pointer cache.
 * 
 * Larger values detect more double-frees but use more memory.
 * The cache uses an LRU eviction policy when full.
 */
#define DOUBLE_FREE_DEFAULT_CACHE_SIZE 4096

/**
 * Configuration options for double-free detection.
 */
typedef struct {
    bool enabled;                   /**< Whether detection is enabled (default: true) */
    bool abort_on_error;            /**< Call abort() on detection (default: false) */
    bool print_on_error;            /**< Print error to stderr (default: true) */
    size_t cache_size;              /**< Number of freed pointers to track (default: 4096) */
    int backtrace_skip_frames;      /**< Frames to skip in backtrace (default: 2) */
} double_free_config_t;

/* ============================================================================
 * Violation Information
 * ============================================================================ */

/**
 * Information about a detected double-free violation.
 * 
 * Contains backtraces from both the original free and the second (invalid) free.
 */
typedef struct {
    void* address;                              /**< The pointer that was double-freed */
    size_t size;                                /**< Size of the original allocation */
    
    /* Original allocation info (if available) */
    const char* alloc_file;                     /**< File where allocated (may be NULL) */
    int alloc_line;                             /**< Line where allocated */
    uint64_t alloc_timestamp;                   /**< When allocated */
    void* alloc_frames[MEMROGUE_MAX_FRAMES];    /**< Allocation backtrace */
    int alloc_frame_count;                      /**< Number of allocation frames */
    
    /* First free info */
    const char* first_free_file;                /**< File of first free (may be NULL) */
    int first_free_line;                        /**< Line of first free */
    uint64_t first_free_timestamp;              /**< When first freed */
    void* first_free_frames[MEMROGUE_MAX_FRAMES]; /**< First free backtrace */
    int first_free_frame_count;                 /**< Number of first free frames */
    
    /* Second (invalid) free info */
    const char* second_free_file;               /**< File of second free (may be NULL) */
    int second_free_line;                       /**< Line of second free */
    uint64_t second_free_timestamp;             /**< When second free attempted */
    void* second_free_frames[MEMROGUE_MAX_FRAMES]; /**< Second free backtrace */
    int second_free_frame_count;                /**< Number of second free frames */
} double_free_violation_t;

/* ============================================================================
 * Double-Free Detector Structure
 * ============================================================================ */

/**
 * Opaque double-free detector handle.
 */
typedef struct double_free_detector_internal double_free_detector_t;

/* ============================================================================
 * Detector Lifecycle
 * ============================================================================ */

/**
 * Initialize configuration with default values.
 * 
 * Defaults:
 *   - enabled: true
 *   - abort_on_error: false
 *   - print_on_error: true
 *   - cache_size: 4096
 *   - backtrace_skip_frames: 2
 * 
 * @param config Configuration structure to initialize
 */
void double_free_config_init(double_free_config_t* config);

/**
 * Create a new double-free detector with default configuration.
 * 
 * @return Newly allocated detector, or NULL on failure
 */
double_free_detector_t* double_free_detector_create(void);

/**
 * Create a new double-free detector with custom configuration.
 * 
 * @param config Configuration options
 * @return Newly allocated detector, or NULL on failure
 */
double_free_detector_t* double_free_detector_create_with_config(
    const double_free_config_t* config);

/**
 * Destroy a double-free detector and free all resources.
 * 
 * @param detector The detector to destroy (may be NULL)
 */
void double_free_detector_destroy(double_free_detector_t* detector);

/* ============================================================================
 * Detection API
 * ============================================================================ */

/**
 * Record a memory allocation for tracking.
 * 
 * This stores the allocation information so it can be included in
 * violation reports if the pointer is double-freed.
 * 
 * Thread-safe: Multiple threads can call this concurrently.
 * 
 * @param detector The double-free detector
 * @param ptr The allocated pointer
 * @param size Size of the allocation
 * @param file Source file (may be NULL)
 * @param line Source line number
 */
void double_free_record_alloc(double_free_detector_t* detector, void* ptr,
                               size_t size, const char* file, int line);

/**
 * Check and record a memory deallocation.
 * 
 * Checks if the pointer was already freed. If not, records this free
 * in the cache for future double-free detection.
 * 
 * Thread-safe: Multiple threads can call this concurrently.
 * 
 * @param detector The double-free detector
 * @param ptr The pointer being freed
 * @param file Source file of free (may be NULL)
 * @param line Source line of free
 * @return true if this is a valid free, false if double-free detected
 */
bool double_free_check_and_record(double_free_detector_t* detector, void* ptr,
                                   const char* file, int line);

/**
 * Remove an allocation record (use when reallocating).
 * 
 * This removes the pointer from both the allocation cache and the
 * freed pointer cache. Use this when realloc() returns a new pointer.
 * 
 * Thread-safe: Multiple threads can call this concurrently.
 * 
 * @param detector The double-free detector
 * @param ptr The pointer to remove
 */
void double_free_remove_record(double_free_detector_t* detector, void* ptr);

/* ============================================================================
 * Violation Handling
 * ============================================================================ */

/**
 * Callback function type for double-free violations.
 * 
 * @param violation Information about the detected violation
 * @param user_data User-provided context
 */
typedef void (*double_free_callback_t)(const double_free_violation_t* violation,
                                        void* user_data);

/**
 * Set a custom callback for double-free violations.
 * 
 * The callback is called immediately when a double-free is detected,
 * before any abort or print action. Use this for custom logging or
 * error handling.
 * 
 * Thread-safe: The callback may be called from any thread.
 * 
 * @param detector The double-free detector
 * @param callback Function to call on violation (NULL to remove)
 * @param user_data Context passed to callback
 */
void double_free_set_callback(double_free_detector_t* detector,
                               double_free_callback_t callback,
                               void* user_data);

/**
 * Get the last detected violation.
 * 
 * Copies the most recent double-free violation to the output parameter.
 * This function is thread-safe.
 * 
 * @param detector The double-free detector
 * @param out_violation Output structure to copy violation data into
 * @return true if a violation was copied, false if no violation detected or invalid params
 */
bool double_free_get_last_violation(
    double_free_detector_t* detector,
    double_free_violation_t* out_violation);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * Statistics about double-free detector operation.
 */
typedef struct {
    uint64_t allocs_recorded;       /**< Total allocations recorded */
    uint64_t frees_recorded;        /**< Total frees recorded */
    uint64_t double_frees_detected; /**< Number of double-frees caught */
    uint64_t cache_evictions;       /**< Number of LRU evictions from cache */
    size_t current_cache_entries;   /**< Current number of cached freed pointers */
} double_free_stats_t;

/**
 * Get current statistics.
 * 
 * @param detector The double-free detector
 * @param out_stats Output structure to fill with statistics
 */
void double_free_get_stats(double_free_detector_t* detector,
                            double_free_stats_t* out_stats);

/**
 * Reset statistics counters.
 * 
 * @param detector The double-free detector
 */
void double_free_reset_stats(double_free_detector_t* detector);

/* ============================================================================
 * Configuration Updates
 * ============================================================================ */

/**
 * Enable or disable detection at runtime.
 * 
 * When disabled, check_and_record() always returns true without checking.
 * 
 * @param detector The double-free detector
 * @param enabled Whether to enable detection
 */
void double_free_set_enabled(double_free_detector_t* detector, bool enabled);

/**
 * Check if detection is currently enabled.
 * 
 * @param detector The double-free detector
 * @return true if detection is enabled
 */
bool double_free_is_enabled(double_free_detector_t* detector);

/**
 * Set abort-on-error behavior.
 * 
 * @param detector The double-free detector
 * @param abort_on_error Whether to abort on double-free detection
 */
void double_free_set_abort_on_error(double_free_detector_t* detector,
                                     bool abort_on_error);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Format a violation as a human-readable string.
 * 
 * The returned string contains detailed information about the double-free,
 * including backtraces if available.
 * 
 * @param violation The violation to format
 * @return Newly allocated string, or NULL on failure.
 *         Caller must free() the returned string.
 */
char* double_free_format_violation(const double_free_violation_t* violation);

/**
 * Print a violation to stderr.
 * 
 * Outputs a formatted error message with backtrace information.
 * 
 * @param violation The violation to print
 */
void double_free_print_violation(const double_free_violation_t* violation);

#endif /* MEMROGUE_DOUBLE_FREE_H */
