/**
 * @file memrogue_leak_detector.h
 * @brief Leak detection engine for identifying unfreed allocations.
 *
 * This module provides an algorithm to detect unfreed allocations by scanning
 * the tracker's hash table. Leaks are grouped by allocation site (backtrace
 * signature) for easier analysis.
 *
 * MEMRO-14: Leak Detection Engine
 */

#ifndef MEMROGUE_LEAK_DETECTOR_H
#define MEMROGUE_LEAK_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memrogue_allocation_record.h"

/* Forward declaration - defined in memrogue_tracker.h */
typedef struct memory_tracker_internal memory_tracker_t;

/* ============================================================================
 * Leak Entry Structure
 * ============================================================================ */

/**
 * Represents a single leaked allocation.
 */
typedef struct leak_entry {
    void* address;                              /**< Leaked memory address */
    size_t size;                                /**< Size of the leaked allocation */
    const char* file;                           /**< Source file (may be NULL) */
    int line;                                   /**< Source line number */
    uint64_t timestamp;                         /**< Allocation timestamp */
    void* frames[MEMROGUE_MAX_FRAMES];          /**< Backtrace frames */
    int frame_count;                            /**< Number of backtrace frames */
    struct leak_entry* next;                    /**< Next entry in the linked list */
} leak_entry_t;

/* ============================================================================
 * Leak Group Structure
 * ============================================================================ */

/**
 * Represents a group of leaks sharing the same allocation site (backtrace).
 *
 * Grouping leaks by backtrace signature helps identify the root cause of
 * memory leaks more efficiently.
 */
typedef struct leak_group {
    uint64_t signature;                         /**< Hash of backtrace frames */
    void* frames[MEMROGUE_MAX_FRAMES];          /**< Representative backtrace */
    int frame_count;                            /**< Number of frames in backtrace */
    const char* file;                           /**< Source file (from first leak) */
    int line;                                   /**< Source line (from first leak) */
    size_t leak_count;                          /**< Number of leaks in this group */
    size_t total_bytes;                         /**< Total bytes leaked in this group */
    leak_entry_t* entries;                      /**< Linked list of leak entries */
    struct leak_group* next;                    /**< Next group in linked list */
} leak_group_t;

/* ============================================================================
 * Leak Report Structure
 * ============================================================================ */

/**
 * Enumeration of leak severity levels.
 */
typedef enum {
    LEAK_SEVERITY_NONE = 0,     /**< No leaks detected */
    LEAK_SEVERITY_LOW,          /**< Minor leaks (< 1KB total) */
    LEAK_SEVERITY_MEDIUM,       /**< Moderate leaks (1KB - 1MB) */
    LEAK_SEVERITY_HIGH,         /**< Significant leaks (1MB - 100MB) */
    LEAK_SEVERITY_CRITICAL      /**< Critical leaks (> 100MB) */
} leak_severity_t;

/**
 * Complete leak detection report.
 *
 * Contains all detected leaks, grouped by allocation site, with summary
 * statistics and severity assessment.
 */
typedef struct {
    /* Summary statistics */
    size_t total_leaks;                 /**< Total number of leaked allocations */
    size_t total_bytes;                 /**< Total bytes leaked */
    size_t group_count;                 /**< Number of unique leak groups */
    leak_severity_t severity;           /**< Overall severity assessment */
    
    /* Grouped leaks */
    leak_group_t* groups;               /**< Linked list of leak groups */
    
    /* Detection metadata */
    uint64_t detection_time_us;         /**< Time taken to detect leaks (microseconds) */
    bool suppression_applied;           /**< Whether any suppressions were applied */
} leak_report_t;

/* ============================================================================
 * Leak Detection Configuration
 * ============================================================================ */

/**
 * Configuration options for leak detection.
 */
typedef struct {
    bool group_by_backtrace;            /**< Group leaks by backtrace signature (default: true) */
    bool include_backtraces;            /**< Include backtraces in report (default: true) */
    size_t max_groups;                  /**< Maximum groups to report (0 = unlimited) */
    size_t max_entries_per_group;       /**< Max entries per group (0 = unlimited) */
    size_t min_leak_size;               /**< Minimum leak size to report (default: 0) */
} leak_detector_config_t;

/* ============================================================================
 * Leak Detection API
 * ============================================================================ */

/**
 * Initialize leak detector configuration with default values.
 *
 * Defaults:
 *   - group_by_backtrace: true
 *   - include_backtraces: true
 *   - max_groups: 0 (unlimited)
 *   - max_entries_per_group: 10
 *   - min_leak_size: 0
 *
 * @param config Configuration structure to initialize
 */
void leak_detector_config_init(leak_detector_config_t* config);

/**
 * Detect memory leaks from the tracker.
 *
 * Scans all active allocations in the tracker and generates a leak report.
 * Leaks are grouped by backtrace signature for easier analysis.
 *
 * This function is thread-safe and takes a snapshot of the tracker state.
 *
 * @param tracker The memory tracker to scan
 * @param config Detection configuration (NULL for defaults)
 * @return Newly allocated leak report, or NULL on failure.
 *         Caller must free with leak_report_destroy().
 */
leak_report_t* leak_detector_scan(memory_tracker_t* tracker,
                                   const leak_detector_config_t* config);

/**
 * Destroy a leak report and free all associated memory.
 *
 * @param report The report to destroy (may be NULL)
 */
void leak_report_destroy(leak_report_t* report);

/* ============================================================================
 * Leak Report Query Functions
 * ============================================================================ */

/**
 * Check if the report contains any leaks.
 *
 * @param report The leak report
 * @return true if leaks were detected, false otherwise
 */
bool leak_report_has_leaks(const leak_report_t* report);

/**
 * Get the severity level as a human-readable string.
 *
 * @param severity The severity level
 * @return Static string describing the severity
 */
const char* leak_severity_to_string(leak_severity_t severity);

/**
 * Get the largest leak group by total bytes.
 *
 * @param report The leak report
 * @return Pointer to the largest group, or NULL if no leaks
 */
const leak_group_t* leak_report_largest_group(const leak_report_t* report);

/**
 * Get the group with the most leak occurrences.
 *
 * @param report The leak report
 * @return Pointer to the most frequent group, or NULL if no leaks
 */
const leak_group_t* leak_report_most_frequent_group(const leak_report_t* report);

/* ============================================================================
 * Backtrace Signature Functions
 * ============================================================================ */

/**
 * Compute a signature (hash) from backtrace frames.
 *
 * The signature is used to group leaks by allocation site.
 *
 * @param frames Array of frame addresses
 * @param frame_count Number of frames
 * @return 64-bit signature hash
 */
uint64_t backtrace_compute_signature(void* const* frames, int frame_count);

/**
 * Compare two backtraces for equality.
 *
 * @param frames1 First backtrace frames
 * @param count1 First backtrace frame count
 * @param frames2 Second backtrace frames
 * @param count2 Second backtrace frame count
 * @return true if backtraces are identical, false otherwise
 */
bool backtrace_equals(void* const* frames1, int count1,
                      void* const* frames2, int count2);

#endif /* MEMROGUE_LEAK_DETECTOR_H */
