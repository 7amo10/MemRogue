/**
 * @file memrogue_cli.c
 * @brief Command-line interface implementation for MemRogue.
 *
 * This module implements the CLI tool for analyzing memory logs and
 * generating leak reports. It provides robust argument parsing, signal
 * handling, and error reporting.
 *
 * Design Goals:
 * - No memory leaks in the CLI tool itself
 * - Graceful handling of signals (SIGINT, SIGTERM)
 * - Clear error messages with exit codes
 * - Support for large log files via streaming
 * - Thread-safe where applicable
 *
 * MEMRO-19: CLI Tool Foundation
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "memrogue_cli.h"
#include "memrogue_report.h"
#include "memrogue_leak_detector.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* ============================================================================
 * Internal Constants
 * ============================================================================ */

/* Buffer sizes */
#define CLI_ERROR_BUFFER_SIZE 512
#define CLI_LINE_BUFFER_SIZE 4096
#define CLI_PATH_MAX 4096

/* ============================================================================
 * Signal Handling State
 * ============================================================================ */

/**
 * Volatile flag for signal handling.
 * Set to 1 when a termination signal is received.
 */
static volatile sig_atomic_t g_termination_requested = 0;

/**
 * Signal handler for SIGINT, SIGTERM, SIGPIPE.
 */
static void cli_signal_handler(int sig) {
    (void)sig;
    g_termination_requested = 1;
}

/* ============================================================================
 * CLI Context (Internal Structure)
 * ============================================================================ */

struct cli_context {
    cli_options_t options;          /**< Parsed options (copy) */
    FILE* output_stream;            /**< Output stream */
    bool output_is_stdout;          /**< True if output is stdout */
    report_formatter_t* formatter;  /**< Report formatter */
    leak_report_t* report;          /**< Generated report */
    size_t total_leaks_found;       /**< Total leaks across all inputs */
    size_t total_bytes_leaked;      /**< Total bytes leaked */
    bool has_errors;                /**< True if errors occurred */
};

/* ============================================================================
 * Options Initialization
 * ============================================================================ */

void cli_options_init(cli_options_t* options) {
    if (options == NULL) {
        return;
    }
    
    memset(options, 0, sizeof(cli_options_t));
    
    /* Default values */
    options->format = CLI_FORMAT_TEXT;
    options->verbosity = CLI_VERBOSITY_NORMAL;
    options->use_colors = isatty(STDOUT_FILENO) != 0;
    options->show_backtraces = true;
    options->max_backtrace_depth = 10;
    options->exit_code_on_leaks = false;
    options->_initialized = true;
}

void cli_options_reset(cli_options_t* options) {
    cli_options_init(options);
}

/* ============================================================================
 * Argument Parsing
 * ============================================================================ */

/* Maximum allowed backtrace depth (configurable limit) */
#define CLI_MAX_BACKTRACE_DEPTH 256

/* Short options string - includes all short options with colons for those requiring arguments */
static const char* const CLI_SHORT_OPTIONS = "hVo:f:vqcCbBsm:d:S:DFe";

/* Long options array */
static const struct option CLI_LONG_OPTIONS[] = {
    /* Help and version */
    {"help",             no_argument,       NULL, 'h'},
    {"version",          no_argument,       NULL, 'V'},
    
    /* Output options */
    {"output",           required_argument, NULL, 'o'},
    {"format",           required_argument, NULL, 'f'},
    {"force",            no_argument,       NULL, 'F'},
    {"color",            no_argument,       NULL, 'c'},
    {"no-color",         no_argument,       NULL, 'C'},
    
    /* Verbosity options */
    {"verbose",          no_argument,       NULL, 'v'},
    {"quiet",            no_argument,       NULL, 'q'},
    {"debug",            no_argument,       NULL, 'D'},
    
    /* Report options */
    {"backtrace",        no_argument,       NULL, 'b'},
    {"no-backtrace",     no_argument,       NULL, 'B'},
    {"summary",          no_argument,       NULL, 's'},
    {"max-leaks",        required_argument, NULL, 'm'},
    {"max-depth",        required_argument, NULL, 'd'},
    
    /* Filter options */
    {"min-size",         required_argument, NULL, 'S'},
    {"file",             required_argument, NULL, 1001},
    {"function",         required_argument, NULL, 1002},
    
    /* Behavior options */
    {"exit-code",        no_argument,       NULL, 'e'},
    
    {NULL, 0, NULL, 0}
};

cli_exit_code_t cli_parse_args(int argc, char* argv[], cli_options_t* options) {
    if (options == NULL) {
        return CLI_EXIT_ERROR;
    }
    
    if (!options->_initialized) {
        cli_options_init(options);
    }
    
    /* Reset getopt state for re-entrant parsing */
    optind = 1;
    opterr = 0;
    
    int opt;
    int option_index = 0;
    char* endptr = NULL;
    
    while ((opt = getopt_long(argc, argv, CLI_SHORT_OPTIONS,
                               CLI_LONG_OPTIONS, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                options->show_help = true;
                return CLI_EXIT_SUCCESS;
                
            case 'V':
                options->show_version = true;
                return CLI_EXIT_SUCCESS;
                
            case 'o':
                options->output_file = optarg;
                break;
                
            case 'f':
                options->format = cli_parse_format(optarg);
                break;
                
            case 'F':
                options->force_overwrite = true;
                break;
                
            case 'c':
                options->use_colors = true;
                break;
                
            case 'C':
                options->use_colors = false;
                break;
                
            case 'v':
                if (options->verbosity < CLI_VERBOSITY_VERBOSE) {
                    options->verbosity = CLI_VERBOSITY_VERBOSE;
                }
                break;
                
            case 'q':
                options->verbosity = CLI_VERBOSITY_QUIET;
                break;
                
            case 'D':
                options->verbosity = CLI_VERBOSITY_DEBUG;
                break;
                
            case 'b':
                options->show_backtraces = true;
                break;
                
            case 'B':
                options->show_backtraces = false;
                break;
                
            case 's':
                options->show_summary_only = true;
                break;
                
            case 'm':
                errno = 0;
                {
                    unsigned long val = strtoul(optarg, &endptr, 10);
                    if (errno != 0 || *endptr != '\0' || val > SIZE_MAX) {
                        cli_error("invalid number for --max-leaks: '%s'", optarg);
                        return CLI_EXIT_USAGE;
                    }
                    options->max_leaks = (size_t)val;
                }
                break;
                
            case 'd':
                errno = 0;
                {
                    unsigned long val = strtoul(optarg, &endptr, 10);
                    if (errno != 0 || *endptr != '\0' || val > SIZE_MAX) {
                        cli_error("invalid number for --max-depth: '%s'", optarg);
                        return CLI_EXIT_USAGE;
                    }
                    options->max_backtrace_depth = (size_t)val;
                }
                break;
                
            case 'S':
                errno = 0;
                {
                    unsigned long val = strtoul(optarg, &endptr, 10);
                    if (errno != 0 || *endptr != '\0' || val > SIZE_MAX) {
                        cli_error("invalid number for --min-size: '%s'", optarg);
                        return CLI_EXIT_USAGE;
                    }
                    options->min_leak_size = (size_t)val;
                }
                break;
                
            case 1001: /* --file */
                options->filter_file = optarg;
                break;
                
            case 1002: /* --function */
                options->filter_function = optarg;
                break;
                
            case 'e':
                options->exit_code_on_leaks = true;
                break;
                
            case '?':
                if (optopt != 0) {
                    cli_error("unknown option: '-%c'", optopt);
                } else {
                    cli_error("unknown option: '%s'", argv[optind - 1]);
                }
                return CLI_EXIT_USAGE;
                
            default:
                cli_error("unexpected option: '%c'", opt);
                return CLI_EXIT_USAGE;
        }
    }
    
    /* Collect remaining arguments as input files */
    for (int i = optind; i < argc && options->input_file_count < CLI_MAX_INPUT_FILES; i++) {
        if (strcmp(argv[i], "-") == 0) {
            options->read_stdin = true;
        } else {
            options->input_files[options->input_file_count++] = argv[i];
        }
    }
    
    return CLI_EXIT_SUCCESS;
}

bool cli_validate_options(const cli_options_t* options,
                          char* error_msg, size_t error_msg_size) {
    if (options == NULL) {
        if (error_msg != NULL && error_msg_size > 0) {
            snprintf(error_msg, error_msg_size, "options is NULL");
        }
        return false;
    }
    
    /* Must have at least one input source (for now, we'll make this optional for future) */
    if (options->input_file_count == 0 && !options->read_stdin) {
        /* This is actually okay - we'll read from stdin by default if no input specified */
    }
    
    /* Check for conflicting options - summary mode implies no backtraces */
    if (options->show_summary_only && options->show_backtraces) {
        /* Note: This is handled in cli_context_create() where show_backtraces
         * is set to false when show_summary_only is true. */
    }
    
    /* Validate max values */
    if (options->max_backtrace_depth > CLI_MAX_BACKTRACE_DEPTH) {
        if (error_msg != NULL && error_msg_size > 0) {
            snprintf(error_msg, error_msg_size,
                     "max-depth cannot exceed %d (prevents excessive memory usage)",
                     CLI_MAX_BACKTRACE_DEPTH);
        }
        return false;
    }
    
    return true;
}

/* ============================================================================
 * Help and Version Output
 * ============================================================================ */

void cli_print_help(FILE* stream) {
    fprintf(stream,
        "%s - %s\n"
        "\n"
        "USAGE:\n"
        "    %s [OPTIONS] [INPUT_FILE...]\n"
        "    %s [OPTIONS] -\n"
        "    command | %s [OPTIONS]\n"
        "\n"
        "DESCRIPTION:\n"
        "    Analyzes memory allocation logs and generates leak reports.\n"
        "    If no input file is specified, reads from standard input.\n"
        "\n"
        "OPTIONS:\n"
        "    -h, --help              Show this help message and exit\n"
        "    -V, --version           Show version information and exit\n"
        "\n"
        "  Output:\n"
        "    -o, --output FILE       Write report to FILE instead of stdout\n"
        "    -f, --format FORMAT     Output format: text, json, csv, summary\n"
        "                            (default: text)\n"
        "    -F, --force             Overwrite output file if it exists\n"
        "    -c, --color             Enable ANSI colors in output\n"
        "    --no-color              Disable ANSI colors in output\n"
        "\n"
        "  Verbosity:\n"
        "    -v, --verbose           Increase verbosity level\n"
        "    -q, --quiet             Suppress informational messages\n"
        "    --debug                 Enable debug output\n"
        "\n"
        "  Report Options:\n"
        "    -b, --backtrace         Include stack traces (default)\n"
        "    -B, --no-backtrace      Exclude stack traces from report\n"
        "    -s, --summary           Show summary only, no leak details\n"
        "    -m, --max-leaks N       Maximum number of leaks to report (0=all)\n"
        "    -d, --max-depth N       Maximum backtrace depth (default: 10)\n"
        "\n"
        "  Filtering:\n"
        "    --min-size BYTES        Only report leaks >= BYTES\n"
        "    --file PATTERN          Only report leaks from matching files\n"
        "    --function PATTERN      Only report leaks from matching functions\n"
        "\n"
        "  Behavior:\n"
        "    -e, --exit-code         Return non-zero exit code if leaks found\n"
        "\n"
        "INPUT:\n"
        "    INPUT_FILE              Log file(s) to analyze\n"
        "    -                       Read from standard input\n"
        "\n"
        "EXIT CODES:\n"
        "    0                       Success (no errors)\n"
        "    1                       General error\n"
        "    2                       Invalid usage or arguments\n"
        "    3                       I/O error (file not found, etc.)\n"
        "    4                       Log parsing error\n"
        "    5                       Memory allocation failure\n"
        "    10                      Leaks detected (with --exit-code)\n"
        "\n"
        "EXAMPLES:\n"
        "    # Analyze a log file and print report\n"
        "    %s memory.log\n"
        "\n"
        "    # Analyze and save to file\n"
        "    %s -o report.txt memory.log\n"
        "\n"
        "    # Analyze from pipe\n"
        "    cat memory.log | %s --summary\n"
        "\n"
        "    # CI integration with exit code\n"
        "    %s --exit-code --quiet memory.log || echo 'Leaks found!'\n"
        "\n"
        "For more information, see: https://github.com/7amo10/MemRogue\n",
        MEMROGUE_CLI_PROGRAM_NAME, MEMROGUE_CLI_DESCRIPTION,
        MEMROGUE_CLI_PROGRAM_NAME, MEMROGUE_CLI_PROGRAM_NAME, MEMROGUE_CLI_PROGRAM_NAME,
        MEMROGUE_CLI_PROGRAM_NAME, MEMROGUE_CLI_PROGRAM_NAME,
        MEMROGUE_CLI_PROGRAM_NAME, MEMROGUE_CLI_PROGRAM_NAME
    );
}

void cli_print_usage(FILE* stream) {
    fprintf(stream,
        "Usage: %s [OPTIONS] [INPUT_FILE...]\n"
        "Try '%s --help' for more information.\n",
        MEMROGUE_CLI_PROGRAM_NAME, MEMROGUE_CLI_PROGRAM_NAME
    );
}

void cli_print_version(FILE* stream) {
    fprintf(stream,
        "%s version %s\n"
        "Copyright (C) 2025 MemRogue Contributors\n"
        "License: MIT\n"
        "\n"
        "Built with:\n"
        "  C Standard: C11\n"
        "  Features: leak detection, double-free detection, backtrace support\n",
        MEMROGUE_CLI_PROGRAM_NAME, MEMROGUE_CLI_VERSION_STRING
    );
}

/* ============================================================================
 * CLI Context Management
 * ============================================================================ */

cli_context_t* cli_context_create(const cli_options_t* options) {
    if (options == NULL) {
        cli_error("options is NULL");
        return NULL;
    }
    
    cli_context_t* ctx = calloc(1, sizeof(cli_context_t));
    if (ctx == NULL) {
        cli_error("failed to allocate context: %s", strerror(errno));
        return NULL;
    }
    
    /* Copy options */
    memcpy(&ctx->options, options, sizeof(cli_options_t));
    
    /* Create report formatter with configuration */
    report_config_t config;
    report_config_init(&config);
    config.show_backtraces = options->show_backtraces && !options->show_summary_only;
    config.use_colors = options->use_colors;
    config.max_backtrace_depth = options->max_backtrace_depth;
    if (options->max_leaks > 0) {
        config.max_groups = options->max_leaks;
    }
    
    ctx->formatter = report_formatter_create_with_config(&config);
    if (ctx->formatter == NULL) {
        cli_error("failed to create report formatter");
        free(ctx);
        return NULL;
    }
    
    /* Create empty report to accumulate results */
    ctx->report = leak_report_create();
    if (ctx->report == NULL) {
        cli_error("failed to create leak report");
        report_formatter_destroy(ctx->formatter);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

void cli_context_destroy(cli_context_t* ctx) {
    if (ctx == NULL) {
        return;
    }
    
    /* Close output stream if we opened it */
    if (ctx->output_stream != NULL && !ctx->output_is_stdout) {
        fclose(ctx->output_stream);
    }
    
    /* Free report and formatter */
    if (ctx->report != NULL) {
        leak_report_destroy(ctx->report);
    }
    if (ctx->formatter != NULL) {
        report_formatter_destroy(ctx->formatter);
    }
    
    free(ctx);
}

/* ============================================================================
 * CLI Execution
 * ============================================================================ */

/**
 * Process a single input file and add leaks to the report.
 *
 * For now, this is a placeholder that creates a mock report.
 * The actual log parsing will be implemented in a future task.
 */
static cli_exit_code_t cli_process_input(cli_context_t* ctx,
                                          FILE* input,
                                          const char* input_name) {
    if (ctx == NULL || input == NULL) {
        return CLI_EXIT_ERROR;
    }
    
    cli_info(ctx->options.verbosity, "Processing: %s", input_name);
    
    /* 
     * TODO: Implement actual log parsing.
     * For now, we just read and count lines to demonstrate the tool works.
     * The log format and parsing will be defined in a future task.
     */
    
    char line[CLI_LINE_BUFFER_SIZE];
    size_t line_count = 0;
    
    while (fgets(line, sizeof(line), input) != NULL) {
        /* Check for termination signal */
        if (cli_termination_requested()) {
            cli_warning("interrupted by signal");
            return CLI_EXIT_SIGNAL;
        }
        
        line_count++;
        
        /* 
         * Future: Parse the line and extract allocation/free info.
         * For now, just count lines.
         */
    }
    
    if (ferror(input)) {
        cli_error("error reading %s: %s", input_name, strerror(errno));
        return CLI_EXIT_IO_ERROR;
    }
    
    cli_debug(ctx->options.verbosity, "Read %zu lines from %s", line_count, input_name);
    
    return CLI_EXIT_SUCCESS;
}

cli_exit_code_t cli_execute(cli_context_t* ctx) {
    if (ctx == NULL) {
        return CLI_EXIT_ERROR;
    }
    
    cli_exit_code_t result = CLI_EXIT_SUCCESS;
    
    /* Open output stream */
    if (ctx->options.output_file != NULL) {
        ctx->output_stream = cli_open_output(ctx->options.output_file,
                                              ctx->options.force_overwrite);
        if (ctx->output_stream == NULL) {
            return CLI_EXIT_IO_ERROR;
        }
        ctx->output_is_stdout = false;
    } else {
        ctx->output_stream = stdout;
        ctx->output_is_stdout = true;
    }
    
    /* Process input files */
    bool has_input = false;
    
    for (size_t i = 0; i < ctx->options.input_file_count; i++) {
        cli_input_type_t input_type;
        FILE* input = cli_open_input(ctx->options.input_files[i], &input_type);
        if (input == NULL) {
            ctx->has_errors = true;
            result = CLI_EXIT_IO_ERROR;
            continue;
        }
        
        has_input = true;
        cli_exit_code_t file_result = cli_process_input(ctx, input,
                                                         ctx->options.input_files[i]);
        cli_close_input(input, input_type);
        
        if (file_result != CLI_EXIT_SUCCESS) {
            ctx->has_errors = true;
            if (file_result == CLI_EXIT_SIGNAL) {
                result = CLI_EXIT_SIGNAL;
                break;
            }
            result = file_result;
        }
    }
    
    /* Process stdin if requested or if no files specified */
    if (ctx->options.read_stdin || (!has_input && ctx->options.input_file_count == 0)) {
        cli_exit_code_t stdin_result = cli_process_input(ctx, stdin, "<stdin>");
        
        if (stdin_result != CLI_EXIT_SUCCESS) {
            ctx->has_errors = true;
            result = stdin_result;
        }
        has_input = true;
    }
    
    if (!has_input) {
        cli_error("no input files specified");
        cli_print_usage(stderr);
        result = CLI_EXIT_USAGE;
        goto cleanup;
    }
    
    /* Check for termination before generating report */
    if (cli_termination_requested()) {
        result = (result != CLI_EXIT_SUCCESS) ? result : CLI_EXIT_SIGNAL;
        goto cleanup;
    }
    
    /* Generate and write the report */
    cli_info(ctx->options.verbosity, "Generating report...");
    
    int bytes_written = report_write_to_stream(ctx->formatter, ctx->report,
                                                ctx->output_stream);
    if (bytes_written < 0) {
        cli_error("failed to write report");
        result = CLI_EXIT_IO_ERROR;
        goto cleanup;
    }
    
    cli_debug(ctx->options.verbosity, "Wrote %d bytes to output", bytes_written);
    
    /* Determine exit code based on leaks found */
    if (ctx->options.exit_code_on_leaks && ctx->report->total_leaks > 0) {
        result = CLI_EXIT_LEAKS_FOUND;
    }

cleanup:
    /* Close output stream if we opened a file (not stdout) */
    if (ctx->output_stream != NULL && !ctx->output_is_stdout) {
        fclose(ctx->output_stream);
        ctx->output_stream = NULL;
    }
    
    return result;
}

int cli_main(int argc, char* argv[]) {
    /* Install signal handlers */
    if (!cli_install_signal_handlers()) {
        cli_warning("failed to install signal handlers");
    }
    
    /* Parse arguments */
    cli_options_t options;
    cli_options_init(&options);
    
    cli_exit_code_t parse_result = cli_parse_args(argc, argv, &options);
    if (parse_result != CLI_EXIT_SUCCESS) {
        cli_print_usage(stderr);
        return (int)parse_result;
    }
    
    /* Handle --help */
    if (options.show_help) {
        cli_print_help(stdout);
        return (int)CLI_EXIT_SUCCESS;
    }
    
    /* Handle --version */
    if (options.show_version) {
        cli_print_version(stdout);
        return (int)CLI_EXIT_SUCCESS;
    }
    
    /* Validate options */
    char error_msg[CLI_ERROR_BUFFER_SIZE];
    if (!cli_validate_options(&options, error_msg, sizeof(error_msg))) {
        cli_error("%s", error_msg);
        return (int)CLI_EXIT_USAGE;
    }
    
    /* Create context and execute */
    cli_context_t* ctx = cli_context_create(&options);
    if (ctx == NULL) {
        return (int)CLI_EXIT_MEMORY_ERROR;
    }
    
    cli_exit_code_t result = cli_execute(ctx);
    
    /* Cleanup */
    cli_context_destroy(ctx);
    
    return (int)result;
}

/* ============================================================================
 * Input/Output Handling
 * ============================================================================ */

FILE* cli_open_input(const char* path, cli_input_type_t* type) {
    if (path == NULL || type == NULL) {
        return NULL;
    }
    
    /* Handle stdin */
    if (strcmp(path, "-") == 0) {
        *type = CLI_INPUT_STDIN;
        return stdin;
    }
    
    /* Check if file exists */
    struct stat st;
    if (stat(path, &st) != 0) {
        cli_error("cannot access '%s': %s", path, strerror(errno));
        return NULL;
    }
    
    /* Check if it's a regular file */
    if (!S_ISREG(st.st_mode)) {
        cli_error("'%s' is not a regular file", path);
        return NULL;
    }
    
    /* Open the file */
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        cli_error("cannot open '%s': %s", path, strerror(errno));
        return NULL;
    }
    
    *type = CLI_INPUT_FILE;
    return file;
}

void cli_close_input(FILE* stream, cli_input_type_t type) {
    if (stream == NULL) {
        return;
    }
    
    /* Don't close stdin */
    if (type == CLI_INPUT_STDIN) {
        return;
    }
    
    fclose(stream);
}

FILE* cli_open_output(const char* path, bool force_overwrite) {
    if (path == NULL) {
        return stdout;
    }
    
    /* Check if file exists */
    struct stat st;
    if (stat(path, &st) == 0 && !force_overwrite) {
        cli_error("output file '%s' already exists (use --force to overwrite)", path);
        return NULL;
    }
    
    /* Open the file */
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        cli_error("cannot create '%s': %s", path, strerror(errno));
        return NULL;
    }
    
    return file;
}

void cli_close_output(FILE* stream, bool is_stdout) {
    if (stream == NULL || is_stdout) {
        return;
    }
    
    fclose(stream);
}

/* ============================================================================
 * Error Reporting
 * ============================================================================ */

void cli_error(const char* format, ...) {
    fprintf(stderr, "%s: error: ", MEMROGUE_CLI_PROGRAM_NAME);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

void cli_warning(const char* format, ...) {
    fprintf(stderr, "%s: warning: ", MEMROGUE_CLI_PROGRAM_NAME);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

void cli_info(cli_verbosity_t verbosity, const char* format, ...) {
    if (verbosity < CLI_VERBOSITY_NORMAL) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

void cli_debug(cli_verbosity_t verbosity, const char* format, ...) {
    if (verbosity < CLI_VERBOSITY_DEBUG) {
        return;
    }
    
    fprintf(stderr, "[DEBUG] ");
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

/* ============================================================================
 * Format Conversion Utilities
 * ============================================================================ */

cli_output_format_t cli_parse_format(const char* format_name) {
    if (format_name == NULL) {
        return CLI_FORMAT_TEXT;
    }
    
    if (strcasecmp(format_name, "text") == 0 ||
        strcasecmp(format_name, "txt") == 0) {
        return CLI_FORMAT_TEXT;
    }
    
    if (strcasecmp(format_name, "json") == 0) {
        return CLI_FORMAT_JSON;
    }
    
    if (strcasecmp(format_name, "csv") == 0) {
        return CLI_FORMAT_CSV;
    }
    
    if (strcasecmp(format_name, "xml") == 0) {
        return CLI_FORMAT_XML;
    }
    
    if (strcasecmp(format_name, "summary") == 0) {
        return CLI_FORMAT_SUMMARY;
    }
    
    cli_warning("unknown format '%s', using text", format_name);
    return CLI_FORMAT_TEXT;
}

const char* cli_format_name(cli_output_format_t format) {
    switch (format) {
        case CLI_FORMAT_TEXT:    return "text";
        case CLI_FORMAT_JSON:    return "json";
        case CLI_FORMAT_CSV:     return "csv";
        case CLI_FORMAT_XML:     return "xml";
        case CLI_FORMAT_SUMMARY: return "summary";
        default:                 return "unknown";
    }
}

const char* cli_format_extension(cli_output_format_t format) {
    switch (format) {
        case CLI_FORMAT_TEXT:    return ".txt";
        case CLI_FORMAT_JSON:    return ".json";
        case CLI_FORMAT_CSV:     return ".csv";
        case CLI_FORMAT_XML:     return ".xml";
        case CLI_FORMAT_SUMMARY: return ".txt";
        default:                 return ".txt";
    }
}

/* ============================================================================
 * Signal Handling
 * ============================================================================ */

bool cli_install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cli_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    
    bool success = true;
    
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        success = false;
    }
    
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        success = false;
    }
    
    /* Ignore SIGPIPE to handle broken pipes gracefully */
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) != 0) {
        success = false;
    }
    
    return success;
}

bool cli_termination_requested(void) {
    return g_termination_requested != 0;
}
