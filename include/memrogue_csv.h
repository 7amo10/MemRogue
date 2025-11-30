/**
 * @file memrogue_csv.h
 * @brief CSV report formatter for MemRogue memory debugger.
 *
 * This module provides CSV output format for leak reports to enable
 * spreadsheet analysis and data processing.
 *
 * Features:
 * - RFC 4180 compliant CSV output
 * - Configurable delimiter and quote characters
 * - Optional header row
 * - Proper CSV escaping (quotes, commas, newlines)
 * - Configurable column selection
 * - Thread-safe formatting
 *
 * CSV Schema (default columns):
 * address,size,timestamp,function,file,line,group_id,total_in_group
 *
 * MEMRO-23: CSV Export Format
 */

#ifndef MEMROGUE_CSV_H
#define MEMROGUE_CSV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "memrogue_leak_detector.h"

/* ============================================================================
 * CSV Schema Version
 * ============================================================================ */

#define MEMROGUE_CSV_SCHEMA_VERSION "1.0"

/* ============================================================================
 * CSV Configuration
 * ============================================================================ */

/**
 * CSV output style options.
 */
typedef enum {
    CSV_STYLE_WITH_HEADER = 0,    /**< Include header row (default) */
    CSV_STYLE_NO_HEADER           /**< No header row */
} csv_style_t;

/**
 * CSV column selection flags.
 * Use bitwise OR to select multiple columns.
 */
typedef enum {
    CSV_COL_NONE            = 0,
    CSV_COL_ADDRESS         = (1 << 0),   /**< Memory address (hex) */
    CSV_COL_SIZE            = (1 << 1),   /**< Allocation size in bytes */
    CSV_COL_TIMESTAMP       = (1 << 2),   /**< Allocation timestamp */
    CSV_COL_FUNCTION        = (1 << 3),   /**< Function name from backtrace */
    CSV_COL_FILE            = (1 << 4),   /**< Source file name */
    CSV_COL_LINE            = (1 << 5),   /**< Source line number */
    CSV_COL_GROUP_ID        = (1 << 6),   /**< Leak group signature/ID */
    CSV_COL_TOTAL_IN_GROUP  = (1 << 7),   /**< Total leaks in this group */
    CSV_COL_TOTAL_BYTES     = (1 << 8),   /**< Total bytes in this group */
    CSV_COL_BACKTRACE       = (1 << 9),   /**< Full backtrace (semicolon-separated) */
    
    /* Convenience combined flags */
    CSV_COL_DEFAULT = (CSV_COL_ADDRESS | CSV_COL_SIZE | CSV_COL_TIMESTAMP |
                       CSV_COL_FUNCTION | CSV_COL_FILE | CSV_COL_LINE),
    CSV_COL_ALL     = ((1 << 10) - 1)
} csv_column_flags_t;

/**
 * Configuration options for CSV formatting.
 */
typedef struct {
    csv_style_t style;              /**< Output style (with/without header) */
    char delimiter;                 /**< Field delimiter character (default: ',') */
    char quote_char;                /**< Quote character for escaping (default: '"') */
    char newline[4];                /**< Line ending (default: "\r\n" per RFC 4180) */
    csv_column_flags_t columns;     /**< Columns to include (default: CSV_COL_DEFAULT) */
    bool quote_all_fields;          /**< Quote all fields, not just those that need it */
    bool include_summary_row;       /**< Add summary row at end */
    size_t max_groups;              /**< Maximum groups to include (0 = all) */
    size_t max_entries_per_group;   /**< Max entries per group (0 = all) */
    size_t max_backtrace_depth;     /**< Max backtrace frames for CSV_COL_BACKTRACE */
} csv_config_t;

/* ============================================================================
 * CSV Formatter Structure
 * ============================================================================ */

/**
 * Opaque CSV formatter handle.
 */
typedef struct csv_formatter_internal csv_formatter_t;

/* ============================================================================
 * Configuration API
 * ============================================================================ */

/**
 * Initialize CSV configuration with default values.
 *
 * Defaults:
 *   - style: CSV_STYLE_WITH_HEADER
 *   - delimiter: ','
 *   - quote_char: '"'
 *   - newline: "\r\n" (RFC 4180 compliant)
 *   - columns: CSV_COL_DEFAULT
 *   - quote_all_fields: false
 *   - include_summary_row: false
 *   - max_groups: 0 (unlimited)
 *   - max_entries_per_group: 0 (unlimited)
 *   - max_backtrace_depth: 10
 *
 * @param config Configuration structure to initialize
 */
void csv_config_init(csv_config_t* config);

/* ============================================================================
 * Formatter Lifecycle
 * ============================================================================ */

/**
 * Create a new CSV formatter with default configuration.
 *
 * @return Newly allocated formatter, or NULL on failure
 */
csv_formatter_t* csv_formatter_create(void);

/**
 * Create a new CSV formatter with custom configuration.
 *
 * @param config Configuration options
 * @return Newly allocated formatter, or NULL on failure
 */
csv_formatter_t* csv_formatter_create_with_config(const csv_config_t* config);

/**
 * Destroy a CSV formatter and free all resources.
 *
 * @param formatter The formatter to destroy (may be NULL)
 */
void csv_formatter_destroy(csv_formatter_t* formatter);

/* ============================================================================
 * CSV Generation API
 * ============================================================================ */

/**
 * Format a leak report as a CSV string.
 *
 * The returned string contains valid CSV with one row per leak entry,
 * optionally with a header row.
 *
 * Thread-safe: Multiple threads can call this concurrently with different
 * formatters or the same formatter (the formatter is read-only during formatting).
 *
 * @param formatter The CSV formatter
 * @param report The leak report to format
 * @return Newly allocated string containing the CSV, or NULL on failure.
 *         Caller must free() the returned string.
 */
char* report_to_csv(csv_formatter_t* formatter, const leak_report_t* report);

/**
 * Write a CSV leak report to a file stream.
 *
 * @param formatter The CSV formatter
 * @param report The leak report to format
 * @param stream Output file stream
 * @return Number of bytes written, or -1 on error
 */
int csv_write_to_stream(csv_formatter_t* formatter,
                        const leak_report_t* report,
                        FILE* stream);

/**
 * Write a CSV leak report to a file.
 *
 * @param formatter The CSV formatter
 * @param report The leak report to format
 * @param filepath Path to the output file
 * @return true on success, false on failure
 */
bool csv_write_to_file(csv_formatter_t* formatter,
                       const leak_report_t* report,
                       const char* filepath);

/* ============================================================================
 * Configuration Update API
 * ============================================================================ */

/**
 * Update formatter configuration.
 *
 * Thread-safe: Uses internal locking if the formatter is used concurrently.
 *
 * @param formatter The CSV formatter
 * @param config New configuration to apply
 */
void csv_formatter_set_config(csv_formatter_t* formatter,
                              const csv_config_t* config);

/**
 * Get current formatter configuration.
 *
 * @param formatter The CSV formatter
 * @param out_config Output structure to fill with current configuration
 */
void csv_formatter_get_config(csv_formatter_t* formatter,
                              csv_config_t* out_config);

/* ============================================================================
 * CSV String Utilities
 * ============================================================================ */

/**
 * Escape a string for CSV output (RFC 4180 compliant).
 *
 * Escapes fields containing delimiter, quote character, or newlines
 * by wrapping in quotes and doubling internal quotes.
 *
 * @param input The string to escape (may be NULL)
 * @param buffer Output buffer
 * @param buffer_size Size of the output buffer
 * @param config CSV configuration (for delimiter and quote char)
 * @return Number of characters written (excluding null terminator).
 *         If buffer is too small, output may be truncated.
 */
size_t csv_escape_field(const char* input, char* buffer, size_t buffer_size,
                        const csv_config_t* config);

/**
 * Escape a string and return a newly allocated copy.
 *
 * @param input The string to escape (may be NULL)
 * @param config CSV configuration (for delimiter and quote char)
 * @return Newly allocated escaped string, or NULL on failure.
 *         Caller must free() the returned string.
 *         Returns empty string "" for NULL input.
 */
char* csv_escape_field_alloc(const char* input, const csv_config_t* config);

/**
 * Check if a string needs escaping for CSV output.
 *
 * A field needs escaping if it contains:
 * - The delimiter character
 * - The quote character
 * - A newline (CR or LF)
 *
 * @param input The string to check (may be NULL)
 * @param config CSV configuration (for delimiter and quote char)
 * @return true if escaping is needed, false otherwise
 */
bool csv_field_needs_escaping(const char* input, const csv_config_t* config);

/**
 * Format a pointer address as a CSV-safe string.
 *
 * @param ptr The pointer to format
 * @param buffer Output buffer
 * @param buffer_size Size of the output buffer
 * @return Pointer to buffer
 */
char* csv_format_address(const void* ptr, char* buffer, size_t buffer_size);

/**
 * Format a timestamp as a human-readable string for CSV.
 *
 * @param timestamp Unix timestamp in microseconds
 * @param buffer Output buffer
 * @param buffer_size Size of the output buffer
 * @return Pointer to buffer, or NULL on error
 */
char* csv_format_timestamp(uint64_t timestamp, char* buffer, size_t buffer_size);

/* ============================================================================
 * Header Generation API
 * ============================================================================ */

/**
 * Generate the CSV header row based on configuration.
 *
 * @param formatter The CSV formatter
 * @return Newly allocated string containing the header row, or NULL on failure.
 *         Caller must free() the returned string.
 */
char* csv_generate_header(csv_formatter_t* formatter);

/**
 * Get the column name for a column flag.
 *
 * @param column The column flag (single flag, not combined)
 * @return Static string with the column name, or NULL if invalid flag
 */
const char* csv_column_name(csv_column_flags_t column);

/* ============================================================================
 * Row Formatting API
 * ============================================================================ */

/**
 * Format a single leak entry as a CSV row.
 *
 * @param formatter The CSV formatter
 * @param entry The leak entry to format
 * @param group The group containing the entry (for group-level data)
 * @return Newly allocated string containing the CSV row, or NULL on failure.
 *         Caller must free() the returned string.
 */
char* csv_format_entry_row(csv_formatter_t* formatter,
                           const leak_entry_t* entry,
                           const leak_group_t* group);

/**
 * Format a summary row with totals.
 *
 * @param formatter The CSV formatter
 * @param report The leak report
 * @return Newly allocated string containing the summary row, or NULL on failure.
 *         Caller must free() the returned string.
 */
char* csv_format_summary_row(csv_formatter_t* formatter,
                             const leak_report_t* report);

#endif /* MEMROGUE_CSV_H */
