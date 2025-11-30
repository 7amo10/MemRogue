/**
 * @file test_json.c
 * @brief Unit tests for JSON formatter.
 *
 * Tests the JSON output format for leak reports, including:
 * - Configuration initialization and defaults
 * - JSON escaping for special characters
 * - Pretty-print and compact output modes
 * - Complete report serialization
 * - Summary and group formatting
 * - Memory safety and edge cases
 *
 * MEMRO-22: JSON Export Format
 */

#include "memrogue_json.h"
#include "memrogue_leak_detector.h"
#include "memrogue_allocation_record.h"
#include "memrogue_backtrace.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================================
 * Test Utilities
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        printf("  Testing: %s... ", #name); \
        fflush(stdout); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("PASSED\n"); \
    } while (0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("FAILED\n"); \
            printf("    Assertion failed: %s\n", #cond); \
            printf("    Location: %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle) \
    do { \
        if (strstr((haystack), (needle)) == NULL) { \
            printf("FAILED\n"); \
            printf("    String does not contain expected substring\n"); \
            printf("    Expected to find: '%s'\n", (needle)); \
            printf("    In string: '%.200s...'\n", (haystack)); \
            printf("    Location: %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define ASSERT_STR_NOT_CONTAINS(haystack, needle) \
    do { \
        if (strstr((haystack), (needle)) != NULL) { \
            printf("FAILED\n"); \
            printf("    String unexpectedly contains substring\n"); \
            printf("    Did not expect to find: '%s'\n", (needle)); \
            printf("    Location: %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

/* ============================================================================
 * Configuration Tests
 * ============================================================================ */

/**
 * Test json_config_init sets proper defaults.
 */
static void test_config_init(void) {
    json_config_t config;
    memset(&config, 0xFF, sizeof(config));  /* Fill with garbage */
    
    json_config_init(&config);
    
    ASSERT(config.style == JSON_STYLE_PRETTY);
    ASSERT(config.indent_width == 2);  /* Default indent width */
    ASSERT(config.include_backtraces == true);
    ASSERT(config.include_addresses == true);
    ASSERT(config.include_metadata == true);
}

/**
 * Test json_config_init with NULL doesn't crash.
 */
static void test_config_init_null(void) {
    /* Should not crash */
    json_config_init(NULL);
}

/* ============================================================================
 * Formatter Creation Tests
 * ============================================================================ */

/**
 * Test creating formatter with default config.
 */
static void test_formatter_create_default(void) {
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    json_formatter_destroy(formatter);
}

/**
 * Test creating formatter with custom config.
 */
static void test_formatter_create_custom(void) {
    json_config_t config;
    json_config_init(&config);
    config.style = JSON_STYLE_COMPACT;
    config.indent_width = 4;
    
    json_formatter_t* formatter = json_formatter_create_with_config(&config);
    ASSERT(formatter != NULL);
    
    json_formatter_destroy(formatter);
}

/**
 * Test destroying NULL formatter doesn't crash.
 */
static void test_formatter_destroy_null(void) {
    json_formatter_destroy(NULL);
}

/* ============================================================================
 * Empty Report Tests
 * ============================================================================ */

/**
 * Test formatting an empty report produces valid JSON.
 */
static void test_format_empty_report(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Verify basic JSON structure */
    ASSERT_STR_CONTAINS(json, "{");
    ASSERT_STR_CONTAINS(json, "}");
    ASSERT_STR_CONTAINS(json, "\"summary\"");
    ASSERT_STR_CONTAINS(json, "\"totalLeaks\":0");
    ASSERT_STR_CONTAINS(json, "\"totalBytes\":0");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/**
 * Test report_to_json with NULL report returns NULL.
 */
static void test_format_null_report(void) {
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, NULL);
    ASSERT(json == NULL);
    
    json_formatter_destroy(formatter);
}

/**
 * Test report_to_json with NULL formatter returns NULL.
 */
static void test_format_null_formatter(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    char* json = report_to_json(NULL, report);
    ASSERT(json == NULL);
    
    leak_report_destroy(report);
}

/* ============================================================================
 * Pretty Print vs Compact Tests
 * ============================================================================ */

/**
 * Test pretty-print output contains newlines and indentation.
 */
static void test_format_pretty_print(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    json_config_t config;
    json_config_init(&config);
    config.style = JSON_STYLE_PRETTY;
    config.indent_width = 2;
    
    json_formatter_t* formatter = json_formatter_create_with_config(&config);
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Pretty print should have newlines */
    ASSERT_STR_CONTAINS(json, "\n");
    /* Pretty print should have indentation */
    ASSERT_STR_CONTAINS(json, "  ");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/**
 * Test compact output has no unnecessary whitespace.
 */
static void test_format_compact(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    json_config_t config;
    json_config_init(&config);
    config.style = JSON_STYLE_COMPACT;
    
    json_formatter_t* formatter = json_formatter_create_with_config(&config);
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Compact mode should still be valid JSON */
    ASSERT_STR_CONTAINS(json, "{");
    ASSERT_STR_CONTAINS(json, "\"summary\"");
    
    /* Compact mode should not have leading indentation */
    ASSERT_STR_NOT_CONTAINS(json, "\n  ");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/* ============================================================================
 * Report with Leaks Tests
 * ============================================================================ */

/**
 * Test formatting a report with leak entries.
 */
static void test_format_report_with_leaks(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    /* Add a leak group using leak_group_create */
    leak_group_t* group = leak_group_create("test.c", 42, false);
    ASSERT(group != NULL);
    group->total_bytes = 1024;
    group->leak_count = 5;
    
    leak_report_add_group(report, group);
    
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Verify leak information is present */
    ASSERT_STR_CONTAINS(json, "\"totalLeaks\":5");
    ASSERT_STR_CONTAINS(json, "\"totalBytes\":1024");
    ASSERT_STR_CONTAINS(json, "\"groups\"");
    ASSERT_STR_CONTAINS(json, "test.c");
    ASSERT_STR_CONTAINS(json, "\"line\":42");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/**
 * Test formatting multiple leak groups.
 */
static void test_format_multiple_groups(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    /* Add first group */
    leak_group_t* group1 = leak_group_create("file1.c", 10, false);
    ASSERT(group1 != NULL);
    group1->total_bytes = 512;
    group1->leak_count = 2;
    leak_report_add_group(report, group1);
    
    /* Add second group */
    leak_group_t* group2 = leak_group_create("file2.c", 20, false);
    ASSERT(group2 != NULL);
    group2->total_bytes = 1024;
    group2->leak_count = 4;
    leak_report_add_group(report, group2);
    
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Both groups should be present */
    ASSERT_STR_CONTAINS(json, "file1.c");
    ASSERT_STR_CONTAINS(json, "file2.c");
    ASSERT_STR_CONTAINS(json, "\"totalLeaks\":6");
    ASSERT_STR_CONTAINS(json, "\"totalBytes\":1536");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/* ============================================================================
 * Backtrace Formatting Tests
 * ============================================================================ */

/**
 * Test backtraces are included when configured.
 */
static void test_format_with_backtraces(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    leak_group_t* group = leak_group_create("main.c", 100, false);
    ASSERT(group != NULL);
    group->total_bytes = 100;
    group->leak_count = 1;
    
    /* Add a mock backtrace */
    group->frame_count = 2;
    group->frames[0] = (void*)0x400100;
    group->frames[1] = (void*)0x400200;
    
    leak_report_add_group(report, group);
    
    json_config_t config;
    json_config_init(&config);
    config.include_backtraces = true;
    config.include_addresses = true;
    
    json_formatter_t* formatter = json_formatter_create_with_config(&config);
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Backtrace should be included */
    ASSERT_STR_CONTAINS(json, "\"backtrace\"");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/**
 * Test backtraces are excluded when configured.
 */
static void test_format_without_backtraces(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    leak_group_t* group = leak_group_create("main.c", 100, false);
    ASSERT(group != NULL);
    group->total_bytes = 100;
    group->leak_count = 1;
    group->frame_count = 2;
    group->frames[0] = (void*)0x400100;
    group->frames[1] = (void*)0x400200;
    
    leak_report_add_group(report, group);
    
    json_config_t config;
    json_config_init(&config);
    config.include_backtraces = false;
    
    json_formatter_t* formatter = json_formatter_create_with_config(&config);
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Backtrace should NOT be included */
    ASSERT_STR_NOT_CONTAINS(json, "\"backtrace\"");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/* ============================================================================
 * Metadata Tests
 * ============================================================================ */

/**
 * Test metadata is included when configured.
 */
static void test_format_with_metadata(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    json_config_t config;
    json_config_init(&config);
    config.include_metadata = true;
    
    json_formatter_t* formatter = json_formatter_create_with_config(&config);
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Metadata should be present */
    ASSERT_STR_CONTAINS(json, "\"generator\"");
    ASSERT_STR_CONTAINS(json, "\"memrogue\"");
    ASSERT_STR_CONTAINS(json, "\"version\"");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/**
 * Test metadata is excluded when configured.
 */
static void test_format_without_metadata(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    json_config_t config;
    json_config_init(&config);
    config.include_metadata = false;
    
    json_formatter_t* formatter = json_formatter_create_with_config(&config);
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Metadata should NOT be present */
    ASSERT_STR_NOT_CONTAINS(json, "\"metadata\"");
    
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/* ============================================================================
 * JSON Escaping Tests
 * ============================================================================ */

/**
 * Test special characters are properly escaped using json_escape_string.
 */
static void test_json_escaping(void) {
    char buffer[256];
    size_t result;
    
    /* Test escaping double quotes */
    result = json_escape_string("hello\"world", buffer, sizeof(buffer));
    ASSERT(result > 0);
    ASSERT_STR_CONTAINS(buffer, "\\\"");
    
    /* Test escaping backslash */
    result = json_escape_string("path\\to\\file", buffer, sizeof(buffer));
    ASSERT(result > 0);
    ASSERT_STR_CONTAINS(buffer, "\\\\");
    
    /* Test escaping newline */
    result = json_escape_string("line1\nline2", buffer, sizeof(buffer));
    ASSERT(result > 0);
    ASSERT_STR_CONTAINS(buffer, "\\n");
    
    /* Test escaping carriage return */
    result = json_escape_string("hello\rworld", buffer, sizeof(buffer));
    ASSERT(result > 0);
    ASSERT_STR_CONTAINS(buffer, "\\r");
    
    /* Test escaping tab */
    result = json_escape_string("col1\tcol2", buffer, sizeof(buffer));
    ASSERT(result > 0);
    ASSERT_STR_CONTAINS(buffer, "\\t");
    
    /* Test NULL input returns 0 */
    result = json_escape_string(NULL, buffer, sizeof(buffer));
    ASSERT(result == 0);
    
    /* Test empty string */
    result = json_escape_string("", buffer, sizeof(buffer));
    ASSERT(result == 0);
    ASSERT(buffer[0] == '\0');
}

/* ============================================================================
 * Formatter Function Tests
 * ============================================================================ */

/**
 * Test json_format_report with formatter (alias for report_to_json).
 */
static void test_json_format_report(void) {
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    ASSERT_STR_CONTAINS(json, "\"summary\"");
    
    free(json);
    leak_report_destroy(report);
    json_formatter_destroy(formatter);
}

/**
 * Test json_format_summary with valid inputs.
 */
static void test_json_format_summary(void) {
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    report->total_leaks = 10;
    report->total_bytes = 2048;
    
    char* json = json_format_summary(formatter, report);
    ASSERT(json != NULL);
    ASSERT_STR_CONTAINS(json, "\"totalLeaks\":10");
    ASSERT_STR_CONTAINS(json, "\"totalBytes\":2048");
    
    free(json);
    leak_report_destroy(report);
    json_formatter_destroy(formatter);
}

/**
 * Test json_format_group with valid inputs.
 */
static void test_json_format_group(void) {
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    leak_group_t* group = leak_group_create("module.c", 50, false);
    ASSERT(group != NULL);
    group->total_bytes = 512;
    group->leak_count = 4;
    
    char* json = json_format_group(formatter, group, 0);
    ASSERT(json != NULL);
    ASSERT_STR_CONTAINS(json, "\"totalBytes\":512");
    ASSERT_STR_CONTAINS(json, "\"leakCount\":4");
    ASSERT_STR_CONTAINS(json, "module.c");
    
    free(json);
    leak_group_destroy(group);
    json_formatter_destroy(formatter);
}

/* ============================================================================
 * Large Report Tests
 * ============================================================================ */

/**
 * Test formatting a report with many leak groups.
 */
static void test_format_large_report(void) {
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    /* Add many groups */
    const size_t num_groups = 100;
    static char file_names[100][32];  /* Static storage for file names */
    
    for (size_t i = 0; i < num_groups; i++) {
        snprintf(file_names[i], sizeof(file_names[i]), "file%zu.c", i);
        leak_group_t* group = leak_group_create(file_names[i], (int)(i * 10), false);
        ASSERT(group != NULL);
        group->total_bytes = (i + 1) * 100;
        group->leak_count = i + 1;
        leak_report_add_group(report, group);
    }
    
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    char* json = report_to_json(formatter, report);
    ASSERT(json != NULL);
    
    /* Verify report is valid */
    ASSERT_STR_CONTAINS(json, "\"groups\"");
    ASSERT_STR_CONTAINS(json, "file0.c");
    ASSERT_STR_CONTAINS(json, "file99.c");
    
    /* Should be able to free without issues */
    free(json);
    json_formatter_destroy(formatter);
    leak_report_destroy(report);
}

/* ============================================================================
 * Write to File Tests
 * ============================================================================ */

/**
 * Test writing JSON to a file.
 */
static void test_write_json_to_file(void) {
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    report->total_leaks = 3;
    report->total_bytes = 768;
    
    /* Use TMPDIR if available, fallback to /tmp */
    const char* tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = "/tmp";
    }
    char test_file[512];
    snprintf(test_file, sizeof(test_file), "%s/test_memrogue_json_output.json", tmpdir);
    
    bool result = json_write_to_file(formatter, report, test_file);
    ASSERT(result == true);
    
    /* Verify file was created and contains valid JSON */
    FILE* f = fopen(test_file, "r");
    ASSERT(f != NULL);
    
    char buffer[4096];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[bytes_read] = '\0';
    fclose(f);  /* Close file before assertions to prevent resource leak */
    
    /* Cleanup file immediately after reading */
    remove(test_file);
    
    /* Now do assertions - cleanup already done */
    ASSERT_STR_CONTAINS(buffer, "\"totalLeaks\":3");
    ASSERT_STR_CONTAINS(buffer, "\"totalBytes\":768");
    
    leak_report_destroy(report);
    json_formatter_destroy(formatter);
}

/**
 * Test writing JSON to invalid path.
 */
static void test_write_json_to_invalid_path(void) {
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    leak_report_t* report = leak_report_create();
    ASSERT(report != NULL);
    
    /* This should fail */
    bool result = json_write_to_file(formatter, report, "/nonexistent/path/file.json");
    ASSERT(result == false);
    
    leak_report_destroy(report);
    json_formatter_destroy(formatter);
}

/* ============================================================================
 * Configuration Access Tests
 * ============================================================================ */

/**
 * Test getting config from formatter.
 */
static void test_formatter_get_config(void) {
    json_config_t config;
    json_config_init(&config);
    config.style = JSON_STYLE_COMPACT;
    config.indent_width = 8;
    config.include_backtraces = false;
    
    json_formatter_t* formatter = json_formatter_create_with_config(&config);
    ASSERT(formatter != NULL);
    
    json_config_t retrieved;
    json_formatter_get_config(formatter, &retrieved);
    ASSERT(retrieved.style == JSON_STYLE_COMPACT);
    ASSERT(retrieved.indent_width == 8);
    ASSERT(retrieved.include_backtraces == false);
    
    json_formatter_destroy(formatter);
}

/**
 * Test setting config on formatter.
 */
static void test_formatter_set_config(void) {
    json_formatter_t* formatter = json_formatter_create();
    ASSERT(formatter != NULL);
    
    json_config_t new_config;
    json_config_init(&new_config);
    new_config.style = JSON_STYLE_COMPACT;
    new_config.indent_width = 4;
    
    json_formatter_set_config(formatter, &new_config);
    
    json_config_t retrieved;
    json_formatter_get_config(formatter, &retrieved);
    ASSERT(retrieved.style == JSON_STYLE_COMPACT);
    ASSERT(retrieved.indent_width == 4);
    
    json_formatter_destroy(formatter);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void) {
    printf("=== MemRogue JSON Formatter Tests ===\n\n");
    
    printf("Configuration Tests:\n");
    TEST(config_init);
    TEST(config_init_null);
    printf("\n");
    
    printf("Formatter Creation Tests:\n");
    TEST(formatter_create_default);
    TEST(formatter_create_custom);
    TEST(formatter_destroy_null);
    printf("\n");
    
    printf("Empty Report Tests:\n");
    TEST(format_empty_report);
    TEST(format_null_report);
    TEST(format_null_formatter);
    printf("\n");
    
    printf("Pretty Print vs Compact Tests:\n");
    TEST(format_pretty_print);
    TEST(format_compact);
    printf("\n");
    
    printf("Report with Leaks Tests:\n");
    TEST(format_report_with_leaks);
    TEST(format_multiple_groups);
    printf("\n");
    
    printf("Backtrace Formatting Tests:\n");
    TEST(format_with_backtraces);
    TEST(format_without_backtraces);
    printf("\n");
    
    printf("Metadata Tests:\n");
    TEST(format_with_metadata);
    TEST(format_without_metadata);
    printf("\n");
    
    printf("JSON Escaping Tests:\n");
    TEST(json_escaping);
    printf("\n");
    
    printf("Formatter Function Tests:\n");
    TEST(json_format_report);
    TEST(json_format_summary);
    TEST(json_format_group);
    printf("\n");
    
    printf("Large Report Tests:\n");
    TEST(format_large_report);
    printf("\n");
    
    printf("Write to File Tests:\n");
    TEST(write_json_to_file);
    TEST(write_json_to_invalid_path);
    printf("\n");
    
    printf("Configuration Access Tests:\n");
    TEST(formatter_get_config);
    TEST(formatter_set_config);
    printf("\n");
    
    printf("=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return (tests_passed == tests_run) ? 0 : 1;
}
