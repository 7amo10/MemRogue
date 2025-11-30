/**
 * @file memrogue_json.h
 * @brief JSON report formatter for MemRogue memory debugger.
 *
 * This module provides JSON output format for leak reports to enable
 * integration with CI/CD tools, log aggregators, and monitoring systems.
 *
 * Features:
 * - Valid JSON schema output
 * - Pretty-printed or compact mode
 * - All leak data and statistics included
 * - Proper string escaping (JSON RFC 8259)
 * - Configurable output options
 * - Thread-safe formatting
 *
 * JSON Schema Overview:
 * {
 *   "version": "1.0",
 *   "generator": "memrogue",
 *   "timestamp": "ISO8601",
 *   "summary": { ... },
 *   "groups": [ ... ],
 *   "metadata": { ... }
 * }
 *
 * MEMRO-22: JSON Export Format
 */

#ifndef MEMROGUE_JSON_H
#define MEMROGUE_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "memrogue_leak_detector.h"

/* ============================================================================
 * JSON Schema Version
 * ============================================================================ */

#define MEMROGUE_JSON_SCHEMA_VERSION "1.0"

/* ============================================================================
 * JSON Configuration
 * ============================================================================ */

/**
 * JSON output style options.
 */
typedef enum {
    JSON_STYLE_COMPACT = 0,     /**< Minified output (no whitespace) */
    JSON_STYLE_PRETTY           /**< Pretty-printed with indentation */
} json_style_t;

/**
 * Configuration options for JSON formatting.
 */
typedef struct {
    json_style_t style;             /**< Output style (compact/pretty) */
    int indent_width;               /**< Indent width for pretty mode (default: 2) */
    bool include_backtraces;        /**< Include backtrace arrays (default: true) */
    bool include_addresses;         /**< Include memory addresses (default: true) */
    bool include_timestamps;        /**< Include allocation timestamps (default: true) */
    bool include_metadata;          /**< Include metadata section (default: true) */
    bool include_entries;           /**< Include individual leak entries (default: true) */
    size_t max_groups;              /**< Maximum groups to include (0 = all) */
    size_t max_entries_per_group;   /**< Max entries per group (0 = all) */
    size_t max_backtrace_depth;     /**< Max backtrace frames (0 = all) */
} json_config_t;

/* ============================================================================
 * JSON Formatter Structure
 * ============================================================================ */

/**
 * Opaque JSON formatter handle.
 */
typedef struct json_formatter_internal json_formatter_t;

/* ============================================================================
 * Configuration API
 * ============================================================================ */

/**
 * Initialize JSON configuration with default values.
 *
 * Defaults:
 *   - style: JSON_STYLE_PRETTY
 *   - indent_width: 2
 *   - include_backtraces: true
 *   - include_addresses: true
 *   - include_timestamps: true
 *   - include_metadata: true
 *   - include_entries: true
 *   - max_groups: 0 (unlimited)
 *   - max_entries_per_group: 0 (unlimited)
 *   - max_backtrace_depth: 0 (unlimited)
 *
 * @param config Configuration structure to initialize
 */
void json_config_init(json_config_t* config);

/* ============================================================================
 * Formatter Lifecycle
 * ============================================================================ */

/**
 * Create a new JSON formatter with default configuration.
 *
 * @return Newly allocated formatter, or NULL on failure
 */
json_formatter_t* json_formatter_create(void);

/**
 * Create a new JSON formatter with custom configuration.
 *
 * @param config Configuration options
 * @return Newly allocated formatter, or NULL on failure
 */
json_formatter_t* json_formatter_create_with_config(const json_config_t* config);

/**
 * Destroy a JSON formatter and free all resources.
 *
 * @param formatter The formatter to destroy (may be NULL)
 */
void json_formatter_destroy(json_formatter_t* formatter);

/* ============================================================================
 * JSON Generation API
 * ============================================================================ */

/**
 * Format a leak report as a JSON string.
 *
 * The returned string contains valid JSON with the complete leak report
 * including summary, groups, entries, and metadata.
 *
 * Thread-safe: Multiple threads can call this concurrently with different
 * formatters or the same formatter (the formatter is read-only during formatting).
 *
 * @param formatter The JSON formatter
 * @param report The leak report to format
 * @return Newly allocated string containing the JSON, or NULL on failure.
 *         Caller must free() the returned string.
 */
char* report_to_json(json_formatter_t* formatter, const leak_report_t* report);

/**
 * Write a JSON leak report to a file stream.
 *
 * @param formatter The JSON formatter
 * @param report The leak report to format
 * @param stream Output file stream
 * @return Number of bytes written, or -1 on error
 */
int json_write_to_stream(json_formatter_t* formatter,
                         const leak_report_t* report,
                         FILE* stream);

/**
 * Write a JSON leak report to a file.
 *
 * @param formatter The JSON formatter
 * @param report The leak report to format
 * @param filepath Path to the output file
 * @return true on success, false on failure
 */
bool json_write_to_file(json_formatter_t* formatter,
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
 * @param formatter The JSON formatter
 * @param config New configuration to apply
 */
void json_formatter_set_config(json_formatter_t* formatter,
                               const json_config_t* config);

/**
 * Get current formatter configuration.
 *
 * @param formatter The JSON formatter
 * @param out_config Output structure to fill with current configuration
 */
void json_formatter_get_config(json_formatter_t* formatter,
                               json_config_t* out_config);

/* ============================================================================
 * JSON String Utilities
 * ============================================================================ */

/**
 * Escape a string for JSON output (RFC 8259 compliant).
 *
 * Escapes special characters: ", \, \b, \f, \n, \r, \t
 * Also escapes control characters as \uXXXX.
 *
 * @param input The string to escape (may be NULL)
 * @param buffer Output buffer
 * @param buffer_size Size of the output buffer
 * @return Number of characters written (excluding null terminator).
 *         If buffer is too small, output may be truncated
 */
size_t json_escape_string(const char* input, char* buffer, size_t buffer_size);

/**
 * Escape a string and return a newly allocated copy.
 *
 * @param input The string to escape (may be NULL)
 * @return Newly allocated escaped string, or NULL on failure.
 *         Caller must free() the returned string.
 *         Returns empty string "" for NULL input.
 */
char* json_escape_string_alloc(const char* input);

/**
 * Format a pointer address as a JSON-safe string.
 *
 * @param ptr The pointer to format
 * @param buffer Output buffer
 * @param buffer_size Size of the output buffer
 * @return Pointer to buffer
 */
char* json_format_address(const void* ptr, char* buffer, size_t buffer_size);

/**
 * Format a timestamp as ISO 8601 string.
 *
 * @param timestamp Unix timestamp in microseconds
 * @param buffer Output buffer
 * @param buffer_size Size of the output buffer
 * @return Pointer to buffer, or NULL on error
 */
char* json_format_timestamp(uint64_t timestamp, char* buffer, size_t buffer_size);

/* ============================================================================
 * Section Formatting API
 * ============================================================================ */

/**
 * Format only the summary section as JSON.
 *
 * @param formatter The JSON formatter
 * @param report The leak report
 * @return Newly allocated JSON string, or NULL on failure. Caller must free().
 */
char* json_format_summary(json_formatter_t* formatter, const leak_report_t* report);

/**
 * Format a single leak group as JSON.
 *
 * @param formatter The JSON formatter
 * @param group The leak group to format
 * @param group_index Index of the group (0-based)
 * @return Newly allocated JSON string, or NULL on failure. Caller must free().
 */
char* json_format_group(json_formatter_t* formatter,
                        const leak_group_t* group,
                        size_t group_index);

/**
 * Format a single leak entry as JSON.
 *
 * @param formatter The JSON formatter
 * @param entry The leak entry to format
 * @return Newly allocated JSON string, or NULL on failure. Caller must free().
 */
char* json_format_entry(json_formatter_t* formatter, const leak_entry_t* entry);

#endif /* MEMROGUE_JSON_H */
