/**
 * @file memrogue_cli.h
 * @brief Command-line interface for MemRogue memory debugger.
 *
 * This module provides the CLI tool functionality for analyzing memory logs
 * and generating reports. Inspired by professional tools like Valgrind,
 * AddressSanitizer, and mtrace.
 *
 * Features:
 * - Argument parsing with getopt_long()
 * - Multiple input sources (files, stdin, pipes)
 * - Multiple output formats (text, JSON, CSV - future)
 * - Configurable report options
 * - Robust error handling
 * - Signal handling for graceful termination
 *
 * Usage Examples:
 *   memrogue-report --help
 *   memrogue-report input.log
 *   memrogue-report -o report.txt input.log
 *   cat input.log | memrogue-report -
 *   memrogue-report --format=text --verbose input.log
 *
 * MEMRO-19: CLI Tool Foundation
 */

#ifndef MEMROGUE_CLI_H
#define MEMROGUE_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* ============================================================================
 * Version Information
 * ============================================================================ */

#define MEMROGUE_CLI_VERSION_MAJOR 1
#define MEMROGUE_CLI_VERSION_MINOR 0
#define MEMROGUE_CLI_VERSION_PATCH 0
#define MEMROGUE_CLI_VERSION_STRING "1.0.0"

#define MEMROGUE_CLI_PROGRAM_NAME "memrogue-report"
#define MEMROGUE_CLI_DESCRIPTION "Memory leak analyzer and report generator"

/* ============================================================================
 * Exit Codes (following Unix conventions)
 * ============================================================================ */

typedef enum {
    CLI_EXIT_SUCCESS = 0,           /**< Successful execution */
    CLI_EXIT_ERROR = 1,             /**< General error */
    CLI_EXIT_USAGE = 2,             /**< Invalid usage/arguments */
    CLI_EXIT_IO_ERROR = 3,          /**< Input/output error */
    CLI_EXIT_PARSE_ERROR = 4,       /**< Log parsing error */
    CLI_EXIT_MEMORY_ERROR = 5,      /**< Memory allocation failure */
    CLI_EXIT_LEAKS_FOUND = 10,      /**< Leaks detected (for CI integration) */
    CLI_EXIT_SIGNAL = 128           /**< Base for signal exit codes */
} cli_exit_code_t;

/* ============================================================================
 * Output Format Options
 * ============================================================================ */

/**
 * Output format for the generated report.
 */
typedef enum {
    CLI_FORMAT_TEXT = 0,            /**< Human-readable text format */
    CLI_FORMAT_JSON,                /**< JSON format for machine parsing (future) */
    CLI_FORMAT_CSV,                 /**< CSV format for spreadsheets (future) */
    CLI_FORMAT_XML,                 /**< XML format (future) */
    CLI_FORMAT_SUMMARY              /**< Brief summary only */
} cli_output_format_t;

/* ============================================================================
 * Verbosity Levels
 * ============================================================================ */

/**
 * Verbosity level for CLI output.
 */
typedef enum {
    CLI_VERBOSITY_QUIET = 0,        /**< Only output errors and the report */
    CLI_VERBOSITY_NORMAL = 1,       /**< Normal output with progress */
    CLI_VERBOSITY_VERBOSE = 2,      /**< Detailed output including debug info */
    CLI_VERBOSITY_DEBUG = 3         /**< Maximum verbosity for debugging */
} cli_verbosity_t;

/* ============================================================================
 * Input Source Types
 * ============================================================================ */

/**
 * Type of input source.
 */
typedef enum {
    CLI_INPUT_FILE = 0,             /**< Regular file */
    CLI_INPUT_STDIN,                /**< Standard input (pipe or redirect) */
    CLI_INPUT_LIVE                  /**< Live connection (future) */
} cli_input_type_t;

/* ============================================================================
 * CLI Options Structure
 * ============================================================================ */

/**
 * Maximum number of input files that can be processed.
 */
#define CLI_MAX_INPUT_FILES 64

/**
 * Command-line options parsed from arguments.
 *
 * All string fields are either NULL or point to static/argv memory
 * (not allocated, should not be freed).
 */
typedef struct {
    /* Input options */
    const char* input_files[CLI_MAX_INPUT_FILES];   /**< Input file paths */
    size_t input_file_count;                        /**< Number of input files */
    bool read_stdin;                                /**< Read from stdin (- argument) */
    
    /* Output options */
    const char* output_file;                        /**< Output file path (NULL = stdout) */
    cli_output_format_t format;                     /**< Output format */
    cli_verbosity_t verbosity;                      /**< Verbosity level */
    bool use_colors;                                /**< Use ANSI colors in output */
    bool force_overwrite;                           /**< Overwrite output file if exists */
    
    /* Report options */
    bool show_backtraces;                           /**< Include backtraces in report */
    bool show_summary_only;                         /**< Only show summary, not details */
    size_t max_leaks;                               /**< Maximum leaks to report (0 = all) */
    size_t max_backtrace_depth;                     /**< Maximum backtrace frames */
    
    /* Filter options */
    size_t min_leak_size;                           /**< Minimum leak size to report */
    const char* filter_file;                        /**< Only show leaks from this file */
    const char* filter_function;                    /**< Only show leaks from this function */
    
    /* Action flags */
    bool show_help;                                 /**< Show help and exit */
    bool show_version;                              /**< Show version and exit */
    bool exit_code_on_leaks;                        /**< Return non-zero exit if leaks found */
    
    /* Internal state */
    bool _initialized;                              /**< Options have been initialized */
} cli_options_t;

/* ============================================================================
 * CLI Context Structure
 * ============================================================================ */

/**
 * CLI execution context containing runtime state.
 *
 * This structure is used internally to manage the CLI execution lifecycle.
 */
typedef struct cli_context cli_context_t;

/* ============================================================================
 * Options Initialization
 * ============================================================================ */

/**
 * Initialize CLI options with default values.
 *
 * Must be called before cli_parse_args() or manually setting options.
 *
 * @param options Options structure to initialize
 */
void cli_options_init(cli_options_t* options);

/**
 * Reset CLI options to default values.
 *
 * @param options Options structure to reset
 */
void cli_options_reset(cli_options_t* options);

/* ============================================================================
 * Argument Parsing
 * ============================================================================ */

/**
 * Parse command-line arguments into options structure.
 *
 * Supports both short (-h) and long (--help) options.
 * Handles multiple input files and the special "-" for stdin.
 *
 * NOT thread-safe: Uses global getopt state. Do not call concurrently
 * from multiple threads.
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param options Output options structure (must be initialized)
 * @return CLI_EXIT_SUCCESS on success, CLI_EXIT_USAGE on invalid arguments
 */
cli_exit_code_t cli_parse_args(int argc, char* argv[], cli_options_t* options);

/**
 * Validate parsed options for consistency.
 *
 * Checks for conflicting options, missing required arguments, etc.
 *
 * @param options Options to validate
 * @param error_msg Buffer to receive error message (may be NULL)
 * @param error_msg_size Size of error message buffer
 * @return true if options are valid, false otherwise
 */
bool cli_validate_options(const cli_options_t* options,
                          char* error_msg, size_t error_msg_size);

/* ============================================================================
 * Help and Version Output
 * ============================================================================ */

/**
 * Print help message to stream.
 *
 * @param stream Output stream (typically stdout)
 */
void cli_print_help(FILE* stream);

/**
 * Print short usage message to stream.
 *
 * @param stream Output stream (typically stderr)
 */
void cli_print_usage(FILE* stream);

/**
 * Print version information to stream.
 *
 * @param stream Output stream (typically stdout)
 */
void cli_print_version(FILE* stream);

/* ============================================================================
 * CLI Execution
 * ============================================================================ */

/**
 * Create a new CLI context.
 *
 * @param options Parsed command-line options
 * @return Newly allocated context, or NULL on failure
 */
cli_context_t* cli_context_create(const cli_options_t* options);

/**
 * Destroy a CLI context and free all resources.
 *
 * Safe to call with NULL.
 *
 * @param ctx Context to destroy
 */
void cli_context_destroy(cli_context_t* ctx);

/**
 * Execute the CLI tool with the given context.
 *
 * This is the main entry point that:
 * 1. Opens input sources
 * 2. Parses log data
 * 3. Generates the report
 * 4. Writes output
 *
 * @param ctx CLI context
 * @return Exit code indicating success or failure
 */
cli_exit_code_t cli_execute(cli_context_t* ctx);

/**
 * Main entry point for the CLI tool.
 *
 * Convenience function that handles the complete lifecycle:
 * 1. Parse arguments
 * 2. Handle --help/--version
 * 3. Execute the tool
 * 4. Clean up
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return Exit code suitable for returning from main()
 */
int cli_main(int argc, char* argv[]);

/* ============================================================================
 * Input Handling
 * ============================================================================ */

/**
 * Open an input source for reading.
 *
 * Handles files and stdin. Returns a FILE* that must be closed
 * with cli_close_input() (not fclose directly for stdin).
 *
 * @param path File path, or "-" for stdin
 * @param type Output parameter for the input type
 * @return FILE pointer, or NULL on error
 */
FILE* cli_open_input(const char* path, cli_input_type_t* type);

/**
 * Close an input source.
 *
 * Safe to call with stdin (will not close it).
 *
 * @param stream The stream to close
 * @param type The type returned from cli_open_input()
 */
void cli_close_input(FILE* stream, cli_input_type_t type);

/**
 * Open an output stream for writing.
 *
 * @param path File path, or NULL for stdout
 * @param force_overwrite If true, overwrite existing files
 * @return FILE pointer, or NULL on error
 */
FILE* cli_open_output(const char* path, bool force_overwrite);

/**
 * Close an output stream.
 *
 * Safe to call with stdout (will not close it).
 *
 * @param stream The stream to close
 * @param is_stdout True if stream is stdout
 */
void cli_close_output(FILE* stream, bool is_stdout);

/* ============================================================================
 * Error Reporting
 * ============================================================================ */

/**
 * Print an error message to stderr with program name prefix.
 *
 * @param format printf-style format string
 * @param ... Format arguments
 */
void cli_error(const char* format, ...);

/**
 * Print a warning message to stderr with program name prefix.
 *
 * @param format printf-style format string
 * @param ... Format arguments
 */
void cli_warning(const char* format, ...);

/**
 * Print an info message to stderr (if verbosity allows).
 *
 * @param verbosity Current verbosity level
 * @param format printf-style format string
 * @param ... Format arguments
 */
void cli_info(cli_verbosity_t verbosity, const char* format, ...);

/**
 * Print a debug message to stderr (if verbosity allows).
 *
 * @param verbosity Current verbosity level
 * @param format printf-style format string
 * @param ... Format arguments
 */
void cli_debug(cli_verbosity_t verbosity, const char* format, ...);

/* ============================================================================
 * Format Conversion Utilities
 * ============================================================================ */

/**
 * Parse a format name string to format enum.
 *
 * @param format_name Format name ("text", "json", "csv", "xml", "summary")
 * @return Format enum, or CLI_FORMAT_TEXT if unknown
 */
cli_output_format_t cli_parse_format(const char* format_name);

/**
 * Get the name of a format enum.
 *
 * @param format Format enum
 * @return Static string with format name
 */
const char* cli_format_name(cli_output_format_t format);

/**
 * Get the file extension for a format.
 *
 * @param format Format enum
 * @return Static string with file extension (e.g., ".txt", ".json")
 */
const char* cli_format_extension(cli_output_format_t format);

/* ============================================================================
 * Signal Handling
 * ============================================================================ */

/**
 * Install signal handlers for graceful termination.
 *
 * Handles SIGINT, SIGTERM, and SIGPIPE.
 *
 * @return true on success, false on failure
 */
bool cli_install_signal_handlers(void);

/**
 * Check if a termination signal has been received.
 *
 * @return true if termination requested
 */
bool cli_termination_requested(void);

#endif /* MEMROGUE_CLI_H */
