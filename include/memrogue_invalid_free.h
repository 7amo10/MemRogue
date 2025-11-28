/**
 * @file memrogue_invalid_free.h
 * @brief Invalid free detection module for MemRogue memory debugger.
 *
 * This module detects attempts to free pointers that were never allocated
 * (or not tracked). It maintains a set of currently allocated pointers and
 * checks each free operation against this set.
 *
 * Features:
 * - Detects invalid free attempts (freeing untracked pointers)
 * - Captures backtrace of the invalid free call
 * - Reports violations with detailed context
 * - Configurable severity level (warning, error, fatal)
 * - Distinguishes from double-free (complements MEMRO-15)
 * - Thread-safe operation
 * - Optional integration with double-free detector
 *
 * MEMRO-16: Invalid Free Detection
 */

#ifndef MEMROGUE_INVALID_FREE_H
#define MEMROGUE_INVALID_FREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memrogue_allocation_record.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * Default initial capacity for the allocation tracking hash table.
 */
#define INVALID_FREE_DEFAULT_CAPACITY 1024

/**
 * Severity levels for invalid free violations.
 * 
 * These determine how the detector responds to violations.
 */
typedef enum {
    INVALID_FREE_SEVERITY_WARNING = 0,  /**< Log warning, continue execution */
    INVALID_FREE_SEVERITY_ERROR = 1,    /**< Log error, continue execution */
    INVALID_FREE_SEVERITY_FATAL = 2     /**< Log error, abort execution */
} invalid_free_severity_t;

/**
 * Configuration options for invalid free detection.
 */
typedef struct {
    bool enabled;                       /**< Whether detection is enabled (default: true) */
    invalid_free_severity_t severity;   /**< Severity level (default: ERROR) */
    bool print_on_error;                /**< Print error to stderr (default: true) */
    size_t initial_capacity;            /**< Initial hash table capacity (default: 1024) */
    int backtrace_skip_frames;          /**< Frames to skip in backtrace (default: 2) */
} invalid_free_config_t;

/* ============================================================================
 * Violation Information
 * ============================================================================ */

/**
 * Type of invalid free detected.
 * 
 * This helps distinguish different causes of invalid free errors.
 */
typedef enum {
    INVALID_FREE_TYPE_UNTRACKED = 0,    /**< Pointer was never tracked (never allocated) */
    INVALID_FREE_TYPE_ALREADY_FREED = 1 /**< Pointer was freed before (double-free via this detector) */
} invalid_free_type_t;

/**
 * Information about a detected invalid free violation.
 * 
 * Contains the invalid pointer address and backtrace of the free call.
 */
typedef struct {
    void* address;                              /**< The pointer that was invalid-freed */
    invalid_free_type_t type;                   /**< Type of invalid free detected */
    
    /* Free call info */
    const char* free_file;                      /**< File of invalid free (may be NULL) */
    int free_line;                              /**< Line of invalid free */
    uint64_t free_timestamp;                    /**< When invalid free attempted */
    void* free_frames[MEMROGUE_MAX_FRAMES];     /**< Free call backtrace */
    int free_frame_count;                       /**< Number of backtrace frames */
    
    /* Additional context for specific violation types */
    const char* context;                        /**< Human-readable context (may be NULL) */
} invalid_free_violation_t;

/* ============================================================================
 * Invalid Free Detector Structure
 * ============================================================================ */

/**
 * Opaque invalid free detector handle.
 */
typedef struct invalid_free_detector_internal invalid_free_detector_t;

/* ============================================================================
 * Detector Lifecycle
 * ============================================================================ */

/**
 * Initialize configuration with default values.
 * 
 * Defaults:
 *   - enabled: true
 *   - severity: INVALID_FREE_SEVERITY_ERROR
 *   - print_on_error: true
 *   - initial_capacity: 1024
 *   - backtrace_skip_frames: 2
 * 
 * @param config Configuration structure to initialize
 */
void invalid_free_config_init(invalid_free_config_t* config);

/**
 * Create a new invalid free detector with default configuration.
 * 
 * @return Newly allocated detector, or NULL on failure
 */
invalid_free_detector_t* invalid_free_detector_create(void);

/**
 * Create a new invalid free detector with custom configuration.
 * 
 * @param config Configuration options
 * @return Newly allocated detector, or NULL on failure
 */
invalid_free_detector_t* invalid_free_detector_create_with_config(
    const invalid_free_config_t* config);

/**
 * Destroy an invalid free detector and free all resources.
 * 
 * @param detector The detector to destroy (may be NULL)
 */
void invalid_free_detector_destroy(invalid_free_detector_t* detector);

/* ============================================================================
 * Detection API
 * ============================================================================ */

/**
 * Record a memory allocation for tracking.
 * 
 * This adds the pointer to the set of valid allocations.
 * 
 * Thread-safe: Multiple threads can call this concurrently.
 * 
 * @param detector The invalid free detector
 * @param ptr The allocated pointer
 * @param size Size of the allocation
 * @param file Source file (may be NULL)
 * @param line Source line number
 */
void invalid_free_record_alloc(invalid_free_detector_t* detector, void* ptr,
                               size_t size, const char* file, int line);

/**
 * Check if a free operation is valid and record it.
 * 
 * Checks if the pointer is in the set of tracked allocations.
 * If valid, removes it from tracking. If invalid, records the violation.
 * 
 * Thread-safe: Multiple threads can call this concurrently.
 * 
 * @param detector The invalid free detector
 * @param ptr The pointer being freed
 * @param file Source file of free (may be NULL)
 * @param line Source line of free
 * @return true if this is a valid free, false if invalid free detected
 */
bool invalid_free_check_and_remove(invalid_free_detector_t* detector, void* ptr,
                                   const char* file, int line);

/**
 * Update allocation record on realloc.
 * 
 * If old_ptr is tracked, removes it. Then adds new_ptr to tracking.
 * Use this when realloc() returns a different pointer.
 * 
 * Thread-safe: Multiple threads can call this concurrently.
 * 
 * @param detector The invalid free detector
 * @param old_ptr The original pointer (may be NULL for new alloc)
 * @param new_ptr The new pointer
 * @param new_size The new allocation size
 * @param file Source file (may be NULL)
 * @param line Source line number
 */
void invalid_free_record_realloc(invalid_free_detector_t* detector,
                                 void* old_ptr, void* new_ptr,
                                 size_t new_size, const char* file, int line);

/**
 * Check if a pointer is currently tracked as allocated.
 * 
 * This does NOT record a violation; use for querying only.
 * 
 * Thread-safe: Multiple threads can call this concurrently.
 * 
 * @param detector The invalid free detector
 * @param ptr The pointer to check
 * @return true if pointer is currently tracked as allocated
 */
bool invalid_free_is_tracked(invalid_free_detector_t* detector, void* ptr);

/* ============================================================================
 * Violation Handling
 * ============================================================================ */

/**
 * Callback function type for invalid free violations.
 * 
 * @param violation Information about the detected violation
 * @param user_data User-provided context
 */
typedef void (*invalid_free_callback_t)(const invalid_free_violation_t* violation,
                                        void* user_data);

/**
 * Set a custom callback for invalid free violations.
 * 
 * The callback is called immediately when an invalid free is detected,
 * before any abort or print action. Use this for custom logging or
 * error handling.
 * 
 * Thread-safe: The callback may be called from any thread.
 * 
 * @param detector The invalid free detector
 * @param callback Function to call on violation (NULL to remove)
 * @param user_data Context passed to callback
 */
void invalid_free_set_callback(invalid_free_detector_t* detector,
                               invalid_free_callback_t callback,
                               void* user_data);

/**
 * Get the last detected violation.
 * 
 * Copies the most recent invalid free violation to the output parameter.
 * This function is thread-safe.
 * 
 * @param detector The invalid free detector
 * @param out_violation Output structure to copy violation data into
 * @return true if a violation was copied, false if no violation detected or invalid params
 */
bool invalid_free_get_last_violation(
    invalid_free_detector_t* detector,
    invalid_free_violation_t* out_violation);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * Statistics about invalid free detector operation.
 */
typedef struct {
    uint64_t allocs_recorded;           /**< Total allocations recorded */
    uint64_t frees_recorded;            /**< Total valid frees recorded */
    uint64_t invalid_frees_detected;    /**< Number of invalid frees caught */
    uint64_t untracked_frees;           /**< Frees of never-allocated pointers */
    uint64_t already_freed_detected;    /**< Frees of already-freed pointers */
    size_t current_allocations;         /**< Current number of tracked allocations */
    size_t peak_allocations;            /**< Peak number of concurrent allocations */
} invalid_free_stats_t;

/**
 * Get current statistics.
 * 
 * @param detector The invalid free detector
 * @param out_stats Output structure to fill with statistics
 */
void invalid_free_get_stats(invalid_free_detector_t* detector,
                            invalid_free_stats_t* out_stats);

/**
 * Reset statistics counters.
 * 
 * Note: Does NOT clear currently tracked allocations, only the counters.
 * 
 * @param detector The invalid free detector
 */
void invalid_free_reset_stats(invalid_free_detector_t* detector);

/* ============================================================================
 * Configuration Updates
 * ============================================================================ */

/**
 * Enable or disable detection at runtime.
 * 
 * When disabled, check_and_remove() always returns true without checking.
 * Allocations are still tracked so detection can be re-enabled later.
 * 
 * @param detector The invalid free detector
 * @param enabled Whether to enable detection
 */
void invalid_free_set_enabled(invalid_free_detector_t* detector, bool enabled);

/**
 * Check if detection is currently enabled.
 * 
 * @param detector The invalid free detector
 * @return true if detection is enabled
 */
bool invalid_free_is_enabled(invalid_free_detector_t* detector);

/**
 * Set the severity level.
 * 
 * @param detector The invalid free detector
 * @param severity The new severity level
 */
void invalid_free_set_severity(invalid_free_detector_t* detector,
                               invalid_free_severity_t severity);

/**
 * Get the current severity level.
 * 
 * @param detector The invalid free detector
 * @return Current severity level
 */
invalid_free_severity_t invalid_free_get_severity(invalid_free_detector_t* detector);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Format a violation as a human-readable string.
 * 
 * The returned string contains detailed information about the invalid free,
 * including backtrace if available.
 * 
 * @param violation The violation to format
 * @return Newly allocated string, or NULL on failure.
 *         Caller must free() the returned string.
 */
char* invalid_free_format_violation(const invalid_free_violation_t* violation);

/**
 * Print a violation to stderr.
 * 
 * Outputs a formatted error message with backtrace information.
 * 
 * @param violation The violation to print
 */
void invalid_free_print_violation(const invalid_free_violation_t* violation);

/**
 * Get a string representation of the violation type.
 * 
 * @param type The violation type
 * @return Static string describing the type
 */
const char* invalid_free_type_to_string(invalid_free_type_t type);

/**
 * Get a string representation of the severity level.
 * 
 * @param severity The severity level
 * @return Static string describing the severity
 */
const char* invalid_free_severity_to_string(invalid_free_severity_t severity);

#endif /* MEMROGUE_INVALID_FREE_H */
