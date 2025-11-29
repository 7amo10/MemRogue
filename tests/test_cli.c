/**
 * @file test_cli.c
 * @brief Unit tests for CLI tool functionality.
 *
 * Tests cover:
 * - Options initialization and parsing
 * - Help and version output
 * - Input/output file handling
 * - Error handling and exit codes
 * - Format parsing utilities
 *
 * MEMRO-19: CLI Tool Foundation
 */

#include "memrogue_cli.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* ============================================================================
 * Test Utilities
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s\n    Condition: %s\n", msg, #cond); \
        return 0; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    tests_run++; \
    fprintf(stderr, "Running %s...\n", #test_func); \
    if (test_func()) { \
        tests_passed++; \
        fprintf(stderr, "  PASS\n"); \
    } else { \
        tests_failed++; \
    } \
} while(0)

/* ============================================================================
 * Test: Options Initialization
 * ============================================================================ */

static int test_cli_options_init(void) {
    cli_options_t options;
    
    /* Initialize options */
    cli_options_init(&options);
    
    /* Verify defaults */
    TEST_ASSERT(options._initialized == true, "should be initialized");
    TEST_ASSERT(options.format == CLI_FORMAT_TEXT, "default format should be text");
    TEST_ASSERT(options.verbosity == CLI_VERBOSITY_NORMAL, "default verbosity should be normal");
    TEST_ASSERT(options.show_backtraces == true, "backtraces should be enabled by default");
    TEST_ASSERT(options.max_backtrace_depth == 10, "max depth should be 10");
    TEST_ASSERT(options.input_file_count == 0, "no input files by default");
    TEST_ASSERT(options.output_file == NULL, "no output file by default");
    TEST_ASSERT(options.show_help == false, "help should be false");
    TEST_ASSERT(options.show_version == false, "version should be false");
    
    return 1;
}

static int test_cli_options_reset(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    /* Modify some values */
    options.format = CLI_FORMAT_JSON;
    options.verbosity = CLI_VERBOSITY_DEBUG;
    options.max_leaks = 100;
    
    /* Reset */
    cli_options_reset(&options);
    
    /* Verify reset to defaults */
    TEST_ASSERT(options.format == CLI_FORMAT_TEXT, "format should be reset to text");
    TEST_ASSERT(options.verbosity == CLI_VERBOSITY_NORMAL, "verbosity should be reset");
    TEST_ASSERT(options.max_leaks == 0, "max_leaks should be reset to 0");
    
    return 1;
}

/* ============================================================================
 * Test: Argument Parsing
 * ============================================================================ */

static int test_cli_parse_args_help(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--help", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.show_help == true, "help flag should be set");
    
    return 1;
}

static int test_cli_parse_args_version(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-V", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.show_version == true, "version flag should be set");
    
    return 1;
}

static int test_cli_parse_args_output_file(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-o", "output.txt", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.output_file != NULL, "output file should be set");
    TEST_ASSERT(strcmp(options.output_file, "output.txt") == 0, "output file should match");
    
    return 1;
}

static int test_cli_parse_args_format(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--format", "json", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.format == CLI_FORMAT_JSON, "format should be JSON");
    
    return 1;
}

static int test_cli_parse_args_verbose(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-v", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.verbosity == CLI_VERBOSITY_VERBOSE, "verbosity should be verbose");
    
    return 1;
}

static int test_cli_parse_args_quiet(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-q", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.verbosity == CLI_VERBOSITY_QUIET, "verbosity should be quiet");
    
    return 1;
}

static int test_cli_parse_args_input_files(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "file1.log", "file2.log", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.input_file_count == 2, "should have 2 input files");
    TEST_ASSERT(strcmp(options.input_files[0], "file1.log") == 0, "first file should match");
    TEST_ASSERT(strcmp(options.input_files[1], "file2.log") == 0, "second file should match");
    
    return 1;
}

static int test_cli_parse_args_stdin(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.read_stdin == true, "stdin flag should be set");
    
    return 1;
}

static int test_cli_parse_args_max_leaks(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--max-leaks", "50", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.max_leaks == 50, "max_leaks should be 50");
    
    return 1;
}

static int test_cli_parse_args_invalid_max_leaks(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--max-leaks", "invalid", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_USAGE, "should fail with usage error");
    
    return 1;
}

static int test_cli_parse_args_summary(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-s", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.show_summary_only == true, "summary flag should be set");
    
    return 1;
}

static int test_cli_parse_args_no_backtrace(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--no-backtrace", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.show_backtraces == false, "backtraces should be disabled");
    
    return 1;
}

static int test_cli_parse_args_combined(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-v", "-o", "out.txt", 
                    "--format", "text", "--max-depth", "5", 
                    "-e", "input.log", NULL};
    int argc = 10;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.verbosity == CLI_VERBOSITY_VERBOSE, "verbosity should be verbose");
    TEST_ASSERT(strcmp(options.output_file, "out.txt") == 0, "output file should match");
    TEST_ASSERT(options.format == CLI_FORMAT_TEXT, "format should be text");
    TEST_ASSERT(options.max_backtrace_depth == 5, "max depth should be 5");
    TEST_ASSERT(options.exit_code_on_leaks == true, "exit code flag should be set");
    TEST_ASSERT(options.input_file_count == 1, "should have 1 input file");
    
    return 1;
}

/* ============================================================================
 * Test: Options Validation
 * ============================================================================ */

static int test_cli_validate_options_valid(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char error_msg[256];
    bool valid = cli_validate_options(&options, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(valid == true, "default options should be valid");
    
    return 1;
}

static int test_cli_validate_options_max_depth(void) {
    cli_options_t options;
    cli_options_init(&options);
    options.max_backtrace_depth = 300; /* Too high - exceeds CLI_MAX_BACKTRACE_DEPTH (256) */
    
    char error_msg[256];
    bool valid = cli_validate_options(&options, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(valid == false, "options with depth > 256 should be invalid");
    TEST_ASSERT(strlen(error_msg) > 0, "error message should be set");
    
    return 1;
}

static int test_cli_validate_options_null(void) {
    char error_msg[256];
    bool valid = cli_validate_options(NULL, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(valid == false, "NULL options should be invalid");
    
    return 1;
}

/* ============================================================================
 * Test: Format Utilities
 * ============================================================================ */

static int test_cli_parse_format_text(void) {
    TEST_ASSERT(cli_parse_format("text") == CLI_FORMAT_TEXT, "text");
    TEST_ASSERT(cli_parse_format("TEXT") == CLI_FORMAT_TEXT, "TEXT");
    TEST_ASSERT(cli_parse_format("txt") == CLI_FORMAT_TEXT, "txt");
    
    return 1;
}

static int test_cli_parse_format_json(void) {
    TEST_ASSERT(cli_parse_format("json") == CLI_FORMAT_JSON, "json");
    TEST_ASSERT(cli_parse_format("JSON") == CLI_FORMAT_JSON, "JSON");
    
    return 1;
}

static int test_cli_parse_format_csv(void) {
    TEST_ASSERT(cli_parse_format("csv") == CLI_FORMAT_CSV, "csv");
    TEST_ASSERT(cli_parse_format("CSV") == CLI_FORMAT_CSV, "CSV");
    
    return 1;
}

static int test_cli_parse_format_summary(void) {
    TEST_ASSERT(cli_parse_format("summary") == CLI_FORMAT_SUMMARY, "summary");
    
    return 1;
}

static int test_cli_parse_format_unknown(void) {
    TEST_ASSERT(cli_parse_format("unknown") == CLI_FORMAT_TEXT, "unknown should default to text");
    TEST_ASSERT(cli_parse_format(NULL) == CLI_FORMAT_TEXT, "NULL should default to text");
    
    return 1;
}

static int test_cli_format_name(void) {
    TEST_ASSERT(strcmp(cli_format_name(CLI_FORMAT_TEXT), "text") == 0, "text");
    TEST_ASSERT(strcmp(cli_format_name(CLI_FORMAT_JSON), "json") == 0, "json");
    TEST_ASSERT(strcmp(cli_format_name(CLI_FORMAT_CSV), "csv") == 0, "csv");
    TEST_ASSERT(strcmp(cli_format_name(CLI_FORMAT_XML), "xml") == 0, "xml");
    TEST_ASSERT(strcmp(cli_format_name(CLI_FORMAT_SUMMARY), "summary") == 0, "summary");
    
    return 1;
}

static int test_cli_format_extension(void) {
    TEST_ASSERT(strcmp(cli_format_extension(CLI_FORMAT_TEXT), ".txt") == 0, "text");
    TEST_ASSERT(strcmp(cli_format_extension(CLI_FORMAT_JSON), ".json") == 0, "json");
    TEST_ASSERT(strcmp(cli_format_extension(CLI_FORMAT_CSV), ".csv") == 0, "csv");
    
    return 1;
}

/* ============================================================================
 * Test: Input/Output Handling
 * ============================================================================ */

static int test_cli_open_input_stdin(void) {
    cli_input_type_t type;
    FILE* stream = cli_open_input("-", &type);
    
    TEST_ASSERT(stream == stdin, "should return stdin");
    TEST_ASSERT(type == CLI_INPUT_STDIN, "type should be stdin");
    
    cli_close_input(stream, type); /* Should not close stdin */
    
    return 1;
}

static int test_cli_open_input_nonexistent(void) {
    cli_input_type_t type;
    FILE* stream = cli_open_input("/nonexistent/file.log", &type);
    
    TEST_ASSERT(stream == NULL, "should fail for nonexistent file");
    
    return 1;
}

static int test_cli_open_output_stdout(void) {
    FILE* stream = cli_open_output(NULL, false);
    
    TEST_ASSERT(stream == stdout, "NULL path should return stdout");
    
    return 1;
}

static int test_cli_close_input_null(void) {
    /* Should not crash */
    cli_close_input(NULL, CLI_INPUT_FILE);
    cli_close_input(NULL, CLI_INPUT_STDIN);
    
    return 1;
}

static int test_cli_close_output_stdout(void) {
    /* Should not close stdout */
    cli_close_output(stdout, true);
    
    /* Verify stdout still works by flushing */
    int result = fflush(stdout);
    TEST_ASSERT(result == 0, "stdout should still be open");
    
    return 1;
}

/* ============================================================================
 * Test: Context Management
 * ============================================================================ */

static int test_cli_context_create_destroy(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    cli_context_t* ctx = cli_context_create(&options);
    TEST_ASSERT(ctx != NULL, "context should be created");
    
    cli_context_destroy(ctx);
    
    return 1;
}

static int test_cli_context_create_null(void) {
    cli_context_t* ctx = cli_context_create(NULL);
    TEST_ASSERT(ctx == NULL, "should fail with NULL options");
    
    return 1;
}

static int test_cli_context_destroy_null(void) {
    /* Should not crash */
    cli_context_destroy(NULL);
    
    return 1;
}

/* ============================================================================
 * Test: Signal Handling
 * ============================================================================ */

static int test_cli_signal_handlers(void) {
    bool result = cli_install_signal_handlers();
    TEST_ASSERT(result == true, "signal handlers should be installed");
    
    /* Initially no termination requested */
    TEST_ASSERT(cli_termination_requested() == false, "no termination initially");
    
    return 1;
}

/* ============================================================================
 * Test: CLI Main
 * ============================================================================ */

static int test_cli_main_help(void) {
    char* argv[] = {"memrogue-report", "--help", NULL};
    int argc = 2;
    
    /* Redirect stdout to /dev/null for this test */
    FILE* devnull = fopen("/dev/null", "w");
    if (devnull == NULL) {
        return 1; /* Skip test if can't open /dev/null */
    }
    
    int old_stdout = dup(STDOUT_FILENO);
    dup2(fileno(devnull), STDOUT_FILENO);
    
    int result = cli_main(argc, argv);
    
    /* Restore stdout */
    dup2(old_stdout, STDOUT_FILENO);
    close(old_stdout);
    fclose(devnull);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "help should exit with success");
    
    return 1;
}

static int test_cli_main_version(void) {
    char* argv[] = {"memrogue-report", "--version", NULL};
    int argc = 2;
    
    /* Redirect stdout to /dev/null */
    FILE* devnull = fopen("/dev/null", "w");
    if (devnull == NULL) {
        return 1;
    }
    
    int old_stdout = dup(STDOUT_FILENO);
    dup2(fileno(devnull), STDOUT_FILENO);
    
    int result = cli_main(argc, argv);
    
    dup2(old_stdout, STDOUT_FILENO);
    close(old_stdout);
    fclose(devnull);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "version should exit with success");
    
    return 1;
}

/* ============================================================================
 * Test: Additional Option Flags (Copilot Review)
 * ============================================================================ */

static int test_cli_parse_args_debug(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-D", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.verbosity == CLI_VERBOSITY_DEBUG, "verbosity should be debug");
    
    return 1;
}

static int test_cli_parse_args_debug_long(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--debug", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.verbosity == CLI_VERBOSITY_DEBUG, "verbosity should be debug");
    
    return 1;
}

static int test_cli_parse_args_color(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-c", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.use_colors == true, "color should be enabled");
    
    return 1;
}

static int test_cli_parse_args_no_color(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-C", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.use_colors == false, "color should be disabled");
    
    return 1;
}

static int test_cli_parse_args_color_long(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--color", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.use_colors == true, "color should be enabled");
    
    return 1;
}

static int test_cli_parse_args_no_color_long(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--no-color", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.use_colors == false, "color should be disabled");
    
    return 1;
}

static int test_cli_parse_args_force(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-F", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.force_overwrite == true, "force overwrite should be enabled");
    
    return 1;
}

static int test_cli_parse_args_force_long(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--force", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.force_overwrite == true, "force overwrite should be enabled");
    
    return 1;
}

static int test_cli_parse_args_min_size(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--min-size", "1024", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.min_leak_size == 1024, "min_leak_size should be 1024");
    
    return 1;
}

static int test_cli_parse_args_min_size_short(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-S", "512", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.min_leak_size == 512, "min_leak_size should be 512");
    
    return 1;
}

static int test_cli_parse_args_file_filter(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--file", "main.c", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.filter_file != NULL, "file filter should be set");
    TEST_ASSERT(strcmp(options.filter_file, "main.c") == 0, "file filter should match");
    
    return 1;
}

static int test_cli_parse_args_function_filter(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "--function", "malloc", NULL};
    int argc = 3;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.filter_function != NULL, "function filter should be set");
    TEST_ASSERT(strcmp(options.filter_function, "malloc") == 0, "function filter should match");
    
    return 1;
}

static int test_cli_parse_args_show_backtrace_short(void) {
    cli_options_t options;
    cli_options_init(&options);
    options.show_backtraces = false; /* Start with disabled */
    
    char* argv[] = {"memrogue-report", "-b", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.show_backtraces == true, "backtraces should be enabled");
    
    return 1;
}

static int test_cli_parse_args_no_backtrace_short(void) {
    cli_options_t options;
    cli_options_init(&options);
    
    char* argv[] = {"memrogue-report", "-B", NULL};
    int argc = 2;
    
    cli_exit_code_t result = cli_parse_args(argc, argv, &options);
    
    TEST_ASSERT(result == CLI_EXIT_SUCCESS, "should succeed");
    TEST_ASSERT(options.show_backtraces == false, "backtraces should be disabled");
    
    return 1;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void) {
    fprintf(stderr, "\n=== CLI Unit Tests ===\n\n");
    
    /* Options tests */
    RUN_TEST(test_cli_options_init);
    RUN_TEST(test_cli_options_reset);
    
    /* Argument parsing tests */
    RUN_TEST(test_cli_parse_args_help);
    RUN_TEST(test_cli_parse_args_version);
    RUN_TEST(test_cli_parse_args_output_file);
    RUN_TEST(test_cli_parse_args_format);
    RUN_TEST(test_cli_parse_args_verbose);
    RUN_TEST(test_cli_parse_args_quiet);
    RUN_TEST(test_cli_parse_args_input_files);
    RUN_TEST(test_cli_parse_args_stdin);
    RUN_TEST(test_cli_parse_args_max_leaks);
    RUN_TEST(test_cli_parse_args_invalid_max_leaks);
    RUN_TEST(test_cli_parse_args_summary);
    RUN_TEST(test_cli_parse_args_no_backtrace);
    RUN_TEST(test_cli_parse_args_combined);
    
    /* Validation tests */
    RUN_TEST(test_cli_validate_options_valid);
    RUN_TEST(test_cli_validate_options_max_depth);
    RUN_TEST(test_cli_validate_options_null);
    
    /* Format utility tests */
    RUN_TEST(test_cli_parse_format_text);
    RUN_TEST(test_cli_parse_format_json);
    RUN_TEST(test_cli_parse_format_csv);
    RUN_TEST(test_cli_parse_format_summary);
    RUN_TEST(test_cli_parse_format_unknown);
    RUN_TEST(test_cli_format_name);
    RUN_TEST(test_cli_format_extension);
    
    /* I/O tests */
    RUN_TEST(test_cli_open_input_stdin);
    RUN_TEST(test_cli_open_input_nonexistent);
    RUN_TEST(test_cli_open_output_stdout);
    RUN_TEST(test_cli_close_input_null);
    RUN_TEST(test_cli_close_output_stdout);
    
    /* Context tests */
    RUN_TEST(test_cli_context_create_destroy);
    RUN_TEST(test_cli_context_create_null);
    RUN_TEST(test_cli_context_destroy_null);
    
    /* Signal tests */
    RUN_TEST(test_cli_signal_handlers);
    
    /* Main function tests */
    RUN_TEST(test_cli_main_help);
    RUN_TEST(test_cli_main_version);
    
    /* Additional option flags tests (Copilot Review) */
    RUN_TEST(test_cli_parse_args_debug);
    RUN_TEST(test_cli_parse_args_debug_long);
    RUN_TEST(test_cli_parse_args_color);
    RUN_TEST(test_cli_parse_args_no_color);
    RUN_TEST(test_cli_parse_args_color_long);
    RUN_TEST(test_cli_parse_args_no_color_long);
    RUN_TEST(test_cli_parse_args_force);
    RUN_TEST(test_cli_parse_args_force_long);
    RUN_TEST(test_cli_parse_args_min_size);
    RUN_TEST(test_cli_parse_args_min_size_short);
    RUN_TEST(test_cli_parse_args_file_filter);
    RUN_TEST(test_cli_parse_args_function_filter);
    RUN_TEST(test_cli_parse_args_show_backtrace_short);
    RUN_TEST(test_cli_parse_args_no_backtrace_short);
    
    /* Summary */
    fprintf(stderr, "\n=== Test Summary ===\n");
    fprintf(stderr, "Total: %d, Passed: %d, Failed: %d\n\n",
            tests_run, tests_passed, tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
