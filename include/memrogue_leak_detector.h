/**
 * @file memrogue_leak_detector.h
 * @brief Leak detection engine for identifying unfreed allocations.
 *
 * This module provides an algorithm to detect unfreed allocations by scanning
 * the tracker's hash table. Leaks are grouped by allocation site (backtrace
 * signature) for easier analysis.
 *
 * The module also provides standalone structure lifecycle management for
 * leak_report_t, leak_group_t, and leak_entry_t, allowing manual construction
 * and manipulation of leak reports.
 *
 * Thread Safety:
 * - All create/clone functions return newly allocated, caller-owned objects
 * - Destroy functions are safe to call from any thread (operate on owned data)
 * - Modification functions (add/remove) are NOT thread-safe; caller must synchronize
 * - The leak_detector_scan() function is thread-safe (takes snapshot under lock)
 *
 * MEMRO-14: Leak Detection Engine
 * MEMRO-18: Leak Report Structure
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
    bool owns_file;                             /**< True if file string is owned (should be freed) */
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
    bool owns_file;                             /**< True if file string is owned (should be freed) */
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

/* ============================================================================
 * Leak Entry Lifecycle Functions (MEMRO-18)
 * ============================================================================ */

/**
 * Create a new leak entry.
 *
 * All fields are initialized to the provided values. The file string is
 * copied if copy_strings is true (and owns_file is set), otherwise it points 
 * to the original (and owns_file is false).
 *
 * @param address Memory address of the leak
 * @param size Size of the leaked allocation in bytes
 * @param file Source file name (may be NULL)
 * @param line Source line number
 * @param copy_strings If true, makes a copy of the file string (entry owns it)
 * @return Newly allocated leak entry, or NULL on failure.
 *         Caller must free with leak_entry_destroy().
 */
leak_entry_t* leak_entry_create(void* address, size_t size,
                                 const char* file, int line,
                                 bool copy_strings);

/**
 * Create a deep copy of a leak entry.
 *
 * The clone owns its file string (if present) and will free it on destroy.
 *
 * @param entry The entry to clone
 * @return Newly allocated clone, or NULL on failure.
 *         Caller must free with leak_entry_destroy().
 */
leak_entry_t* leak_entry_clone(const leak_entry_t* entry);

/**
 * Destroy a single leak entry.
 *
 * Frees the file string if entry owns it (owns_file == true).
 * Does NOT follow the next pointer; only destroys this entry.
 *
 * @param entry The entry to destroy (may be NULL)
 */
void leak_entry_destroy(leak_entry_t* entry);

/**
 * Destroy a chain of leak entries (follows next pointers).
 *
 * @param entry Head of the chain to destroy (may be NULL)
 */
void leak_entry_destroy_chain(leak_entry_t* entry);

/**
 * Set backtrace frames on an entry.
 *
 * @param entry The leak entry
 * @param frames Array of frame addresses
 * @param frame_count Number of frames (capped at MEMROGUE_MAX_FRAMES)
 */
void leak_entry_set_backtrace(leak_entry_t* entry,
                               void* const* frames, int frame_count);

/**
 * Count entries in a chain.
 *
 * @param entry Head of the chain (may be NULL)
 * @return Number of entries in the chain
 */
size_t leak_entry_chain_count(const leak_entry_t* entry);

/* ============================================================================
 * Leak Group Lifecycle Functions (MEMRO-18)
 * ============================================================================ */

/**
 * Create a new empty leak group.
 *
 * @param file Source file name for the group (may be NULL)
 * @param line Source line number for the group
 * @param copy_strings If true, makes a copy of the file string (group owns it)
 * @return Newly allocated leak group, or NULL on failure.
 *         Caller must free with leak_group_destroy().
 */
leak_group_t* leak_group_create(const char* file, int line, bool copy_strings);

/**
 * Create a deep copy of a leak group including all entries.
 *
 * The clone owns its file string and all cloned entries will own their
 * file strings. All owned strings will be freed on destroy.
 *
 * @param group The group to clone
 * @return Newly allocated clone, or NULL on failure.
 *         Caller must free with leak_group_destroy().
 */
leak_group_t* leak_group_clone(const leak_group_t* group);

/**
 * Destroy a single leak group and all its entries.
 *
 * Frees the file string if group owns it (owns_file == true).
 * Does NOT follow the next pointer; only destroys this group.
 *
 * @param group The group to destroy (may be NULL)
 */
void leak_group_destroy(leak_group_t* group);

/**
 * Destroy a chain of leak groups (follows next pointers).
 *
 * @param group Head of the chain to destroy (may be NULL)
 */
void leak_group_destroy_chain(leak_group_t* group);

/**
 * Add a leak entry to a group.
 *
 * The entry is prepended to the group's entry list. The group takes ownership
 * of the entry. Updates leak_count and total_bytes statistics.
 *
 * NOT thread-safe; caller must synchronize if accessed from multiple threads.
 *
 * @param group The group to add to
 * @param entry The entry to add (ownership transferred)
 * @return true on success, false if group or entry is NULL
 */
bool leak_group_add_entry(leak_group_t* group, leak_entry_t* entry);

/**
 * Remove and return the first entry from a group.
 *
 * Updates leak_count and total_bytes statistics.
 *
 * NOT thread-safe; caller must synchronize if accessed from multiple threads.
 *
 * @param group The group to remove from
 * @return The removed entry (caller owns), or NULL if group is empty
 */
leak_entry_t* leak_group_pop_entry(leak_group_t* group);

/**
 * Set backtrace frames on a group (representative backtrace).
 *
 * Also recomputes the signature based on the new backtrace.
 *
 * @param group The leak group
 * @param frames Array of frame addresses
 * @param frame_count Number of frames (capped at MEMROGUE_MAX_FRAMES)
 */
void leak_group_set_backtrace(leak_group_t* group,
                               void* const* frames, int frame_count);

/**
 * Recalculate group statistics from its entries.
 *
 * Useful after manually modifying entries. Updates leak_count and total_bytes.
 *
 * @param group The group to recalculate
 */
void leak_group_recalculate_stats(leak_group_t* group);

/* ============================================================================
 * Leak Report Lifecycle Functions (MEMRO-18)
 * ============================================================================ */

/**
 * Create a new empty leak report.
 *
 * @return Newly allocated leak report, or NULL on failure.
 *         Caller must free with leak_report_destroy().
 */
leak_report_t* leak_report_create(void);

/**
 * Create a deep copy of a leak report including all groups and entries.
 *
 * All cloned groups and entries will own their file strings, which will
 * be freed when the report is destroyed.
 *
 * @param report The report to clone
 * @return Newly allocated clone, or NULL on failure.
 *         Caller must free with leak_report_destroy().
 */
leak_report_t* leak_report_clone(const leak_report_t* report);

/**
 * Add a leak group to a report.
 *
 * The group is prepended to the report's group list. The report takes ownership
 * of the group. Updates group_count, total_leaks, and total_bytes.
 * Severity is recalculated based on new totals.
 *
 * NOT thread-safe; caller must synchronize if accessed from multiple threads.
 *
 * @param report The report to add to
 * @param group The group to add (ownership transferred)
 * @return true on success, false if report or group is NULL
 */
bool leak_report_add_group(leak_report_t* report, leak_group_t* group);

/**
 * Remove and return the first group from a report.
 *
 * Updates group_count, total_leaks, total_bytes, and severity.
 *
 * NOT thread-safe; caller must synchronize if accessed from multiple threads.
 *
 * @param report The report to remove from
 * @return The removed group (caller owns), or NULL if report is empty
 */
leak_group_t* leak_report_pop_group(leak_report_t* report);

/**
 * Find a group in the report by backtrace signature.
 *
 * @param report The report to search
 * @param signature The backtrace signature to find
 * @return Pointer to the matching group, or NULL if not found
 */
leak_group_t* leak_report_find_group_by_signature(leak_report_t* report,
                                                   uint64_t signature);

/**
 * Recalculate report statistics from its groups.
 *
 * Useful after manually modifying groups. Updates total_leaks, total_bytes,
 * group_count, and severity.
 *
 * @param report The report to recalculate
 */
void leak_report_recalculate_stats(leak_report_t* report);

/**
 * Merge another report into this one.
 *
 * Groups with matching signatures are merged (entries combined).
 * Groups without matches are cloned and added.
 * The source report is not modified.
 *
 * NOT thread-safe; caller must synchronize if accessed from multiple threads.
 *
 * @note On allocation failure, dest may contain a partial merge. Caller
 *       should destroy dest if atomicity is required.
 *
 * @param dest The destination report (modified)
 * @param src The source report (not modified)
 * @return true on success, false on allocation failure
 */
bool leak_report_merge(leak_report_t* dest, const leak_report_t* src);

/**
 * Clear all groups from a report.
 *
 * Destroys all groups and entries, resets statistics to zero.
 *
 * @param report The report to clear
 */
void leak_report_clear(leak_report_t* report);

/**
 * Callback type for iterating over leak entries in a report.
 *
 * @param entry The current leak entry
 * @param group The group containing the entry
 * @param user_data User-provided context
 * @return true to continue iteration, false to stop
 */
typedef bool (*leak_entry_iterator_fn)(const leak_entry_t* entry,
                                        const leak_group_t* group,
                                        void* user_data);

/**
 * Iterate over all leak entries in a report.
 *
 * Calls the callback for each entry in each group.
 *
 * @param report The report to iterate
 * @param callback Function to call for each entry
 * @param user_data User-provided context passed to callback
 * @return Number of entries visited, or -1 if callback returned false
 */
int leak_report_iterate_entries(const leak_report_t* report,
                                 leak_entry_iterator_fn callback,
                                 void* user_data);

#endif /* MEMROGUE_LEAK_DETECTOR_H */
