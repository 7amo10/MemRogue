/**
 * @file memrogue_report.h
 * @brief Text report formatter for MemRogue memory debugger.
 *
 * This module provides human-readable text report generation for leak reports.
 * Reports include summary statistics, grouped leaks with backtraces, and totals.
 *
 * Features:
 * - Summary section with totals and severity
 * - Grouped leak entries with backtraces
 * - Sorted by size (largest first)
 * - Readable formatting with indentation
 * - File:line information included
 * - Percentage of total calculated
 * - Configurable output options
 *
 * MEMRO-17: Text Report Formatter
 */

#ifndef MEMROGUE_REPORT_H
#define MEMROGUE_REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "memrogue_leak_detector.h"

/* ============================================================================
 * Report Configuration
 * ============================================================================ */

/**
 * Sort order for leak groups in the report.
 */
typedef enum {
    REPORT_SORT_BY_SIZE = 0,        /**< Sort by total bytes (largest first) */
    REPORT_SORT_BY_COUNT,           /**< Sort by leak count (most frequent first) */
    REPORT_SORT_BY_LOCATION         /**< Sort by source location (file:line) */
} report_sort_order_t;

/**
 * Configuration options for report formatting.
 */
typedef struct {
    bool show_summary;              /**< Include summary section (default: true) */
    bool show_backtraces;           /**< Include backtraces (default: true) */
    bool show_percentages;          /**< Show percentage of total (default: true) */
    bool show_timestamps;           /**< Show allocation timestamps (default: false) */
    bool show_addresses;            /**< Show memory addresses (default: true) */
    bool use_colors;                /**< Use ANSI colors in output (default: false) */
    report_sort_order_t sort_order; /**< Sort order for groups (default: BY_SIZE) */
    size_t max_groups;              /**< Maximum groups to show (0 = all) */
    size_t max_entries_per_group;   /**< Max entries per group (0 = all) */
    size_t max_backtrace_depth;     /**< Max backtrace frames to show (0 = all) */
    int indent_width;               /**< Indentation width in spaces (default: 2) */
} report_config_t;

/* ============================================================================
 * Report Formatter Structure
 * ============================================================================ */

/**
 * Opaque report formatter handle.
 */
typedef struct report_formatter_internal report_formatter_t;

/* ============================================================================
 * Configuration API
 * ============================================================================ */

/**
 * Initialize report configuration with default values.
 *
 * Defaults:
 *   - show_summary: true
 *   - show_backtraces: true
 *   - show_percentages: true
 *   - show_timestamps: false
 *   - show_addresses: true
 *   - use_colors: false
 *   - sort_order: REPORT_SORT_BY_SIZE
 *   - max_groups: 0 (unlimited)
 *   - max_entries_per_group: 5
 *   - max_backtrace_depth: 10
 *   - indent_width: 2
 *
 * @param config Configuration structure to initialize
 */
void report_config_init(report_config_t* config);

/* ============================================================================
 * Formatter Lifecycle
 * ============================================================================ */

/**
 * Create a new report formatter with default configuration.
 *
 * @return Newly allocated formatter, or NULL on failure
 */
report_formatter_t* report_formatter_create(void);

/**
 * Create a new report formatter with custom configuration.
 *
 * @param config Configuration options
 * @return Newly allocated formatter, or NULL on failure
 */
report_formatter_t* report_formatter_create_with_config(const report_config_t* config);

/**
 * Destroy a report formatter and free all resources.
 *
 * @param formatter The formatter to destroy (may be NULL)
 */
void report_formatter_destroy(report_formatter_t* formatter);

/* ============================================================================
 * Report Generation API
 * ============================================================================ */

/**
 * Format a leak report as a human-readable string.
 *
 * The returned string contains the complete formatted report including
 * summary, grouped leaks, and statistics.
 *
 * Thread-safe: Multiple threads can call this concurrently with different
 * formatters or the same formatter (the formatter is read-only during formatting).
 *
 * @param formatter The report formatter
 * @param report The leak report to format
 * @return Newly allocated string containing the formatted report, or NULL on failure.
 *         Caller must free() the returned string.
 */
char* report_format_text(report_formatter_t* formatter, const leak_report_t* report);

/**
 * Write a formatted leak report to a file stream.
 *
 * @param formatter The report formatter
 * @param report The leak report to format
 * @param stream Output file stream (e.g., stdout, stderr, or file)
 * @return Number of bytes written, or -1 on error
 */
int report_write_to_stream(report_formatter_t* formatter,
                           const leak_report_t* report,
                           FILE* stream);

/**
 * Write a formatted leak report to a file.
 *
 * @param formatter The report formatter
 * @param report The leak report to format
 * @param filepath Path to the output file
 * @return true on success, false on failure
 */
bool report_write_to_file(report_formatter_t* formatter,
                          const leak_report_t* report,
                          const char* filepath);

/* ============================================================================
 * Section Formatting API
 * ============================================================================ */

/**
 * Format only the summary section.
 *
 * @param formatter The report formatter
 * @param report The leak report
 * @return Newly allocated string, or NULL on failure. Caller must free().
 */
char* report_format_summary(report_formatter_t* formatter, const leak_report_t* report);

/**
 * Format only a single leak group.
 *
 * @param formatter The report formatter
 * @param group The leak group to format
 * @param total_bytes Total bytes for percentage calculation
 * @param group_rank Rank of this group (1-based) for display
 * @return Newly allocated string, or NULL on failure. Caller must free().
 */
char* report_format_group(report_formatter_t* formatter,
                          const leak_group_t* group,
                          size_t total_bytes,
                          size_t group_rank);

/**
 * Format a backtrace as a string.
 *
 * @param formatter The report formatter
 * @param frames Array of frame addresses
 * @param frame_count Number of frames
 * @return Newly allocated string, or NULL on failure. Caller must free().
 */
char* report_format_backtrace(report_formatter_t* formatter,
                              void* const* frames,
                              int frame_count);

/* ============================================================================
 * Configuration Update API
 * ============================================================================ */

/**
 * Update formatter configuration.
 *
 * Thread-safe: Uses internal locking if the formatter is used concurrently.
 *
 * @param formatter The report formatter
 * @param config New configuration to apply
 */
void report_formatter_set_config(report_formatter_t* formatter,
                                 const report_config_t* config);

/**
 * Get current formatter configuration.
 *
 * @param formatter The report formatter
 * @param out_config Output structure to fill with current configuration
 */
void report_formatter_get_config(report_formatter_t* formatter,
                                 report_config_t* out_config);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Format a byte size as a human-readable string (e.g., "1.5 KB", "32 MB").
 *
 * @param bytes Size in bytes
 * @param buffer Output buffer
 * @param buffer_size Size of the output buffer
 * @return Pointer to buffer, or NULL on error
 */
char* format_bytes(size_t bytes, char* buffer, size_t buffer_size);

/**
 * Get sort order as a human-readable string.
 *
 * @param order The sort order
 * @return Static string describing the sort order
 */
const char* report_sort_order_to_string(report_sort_order_t order);

#endif /* MEMROGUE_REPORT_H */
