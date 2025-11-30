/**
 * @file test_csv.c
 * @brief Tests for CSV report formatter.
 *
 * Tests CSV output generation including escaping, headers, column selection,
 * and formatting options per RFC 4180 compliance.
 *
 * MEMRO-23: CSV Export Format
 */

#include "memrogue_csv.h"
#include "memrogue_leak_detector.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Test Counters
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        printf("  Testing: %s... ", #name); \
        fflush(stdout); \
    } while (0)

#define PASS() \
    do { \
        printf("PASSED\n"); \
        tests_passed++; \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAILED: %s\n", msg); \
        tests_failed++; \
    } while (0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            FAIL(msg); \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)

#define ASSERT_NOT_NULL(ptr, msg) ASSERT_TRUE((ptr) != NULL, msg)
#define ASSERT_NULL(ptr, msg) ASSERT_TRUE((ptr) == NULL, msg)

#define ASSERT_STR_CONTAINS(haystack, needle, msg) \
    do { \
        if (!haystack || !strstr(haystack, needle)) { \
            printf("\n    Expected to contain: '%s'\n    Got: '%s'\n", \
                   needle, haystack ? haystack : "(null)"); \
            FAIL(msg); \
            return; \
        } \
    } while (0)

#define ASSERT_STR_EQUALS(actual, expected, msg) \
    do { \
        if (!actual || !expected || strcmp(actual, expected) != 0) { \
            printf("\n    Expected: '%s'\n    Got: '%s'\n", \
                   expected ? expected : "(null)", \
                   actual ? actual : "(null)"); \
            FAIL(msg); \
            return; \
        } \
    } while (0)

/* ============================================================================
 * Configuration Tests
 * ============================================================================ */

/**
 * Test csv_config_init sets proper defaults.
 */
static void test_config_init_defaults(void) {
    TEST(config_init_defaults);
    
    csv_config_t config;
    csv_config_init(&config);
    
    ASSERT_TRUE(config.style == CSV_STYLE_WITH_HEADER, "default style should be with header");
    ASSERT_TRUE(config.delimiter == ',', "default delimiter should be comma");
    ASSERT_TRUE(config.quote_char == '"', "default quote char should be double quote");
    ASSERT_TRUE(strcmp(config.newline, "\r\n") == 0, "default newline should be CRLF");
    ASSERT_TRUE(config.columns == CSV_COL_DEFAULT, "default columns should be CSV_COL_DEFAULT");
    ASSERT_FALSE(config.quote_all_fields, "quote_all_fields should be false");
    ASSERT_FALSE(config.include_summary_row, "include_summary_row should be false");
    ASSERT_TRUE(config.max_groups == 0, "max_groups should be 0 (unlimited)");
    ASSERT_TRUE(config.max_entries_per_group == 0, "max_entries_per_group should be 0");
    ASSERT_TRUE(config.max_backtrace_depth == 10, "max_backtrace_depth should be 10");
    
    PASS();
}

/**
 * Test csv_config_init with NULL is safe.
 */
static void test_config_init_null(void) {
    TEST(config_init_null);
    
    /* Should not crash */
    csv_config_init(NULL);
    
    PASS();
}

/* ============================================================================
 * Formatter Lifecycle Tests
 * ============================================================================ */

/**
 * Test formatter creation with default config.
 */
static void test_formatter_create_default(void) {
    TEST(formatter_create_default);
    
    csv_formatter_t* formatter = csv_formatter_create();
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test formatter creation with custom config.
 */
static void test_formatter_create_with_config(void) {
    TEST(formatter_create_with_config);
    
    csv_config_t config;
    csv_config_init(&config);
    config.delimiter = ';';
    config.quote_all_fields = true;
    
    csv_formatter_t* formatter = csv_formatter_create_with_config(&config);
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    csv_config_t out_config;
    csv_formatter_get_config(formatter, &out_config);
    ASSERT_TRUE(out_config.delimiter == ';', "delimiter should be semicolon");
    ASSERT_TRUE(out_config.quote_all_fields, "quote_all_fields should be true");
    
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test formatter creation with NULL config fails.
 */
static void test_formatter_create_null_config(void) {
    TEST(formatter_create_null_config);
    
    csv_formatter_t* formatter = csv_formatter_create_with_config(NULL);
    ASSERT_NULL(formatter, "formatter should not be created with NULL config");
    
    PASS();
}

/**
 * Test formatter destroy with NULL is safe.
 */
static void test_formatter_destroy_null(void) {
    TEST(formatter_destroy_null);
    
    /* Should not crash */
    csv_formatter_destroy(NULL);
    
    PASS();
}

/**
 * Test config get/set.
 */
static void test_formatter_config_update(void) {
    TEST(formatter_config_update);
    
    csv_formatter_t* formatter = csv_formatter_create();
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    csv_config_t new_config;
    csv_config_init(&new_config);
    new_config.style = CSV_STYLE_NO_HEADER;
    new_config.delimiter = '\t';
    
    csv_formatter_set_config(formatter, &new_config);
    
    csv_config_t out_config;
    csv_formatter_get_config(formatter, &out_config);
    ASSERT_TRUE(out_config.style == CSV_STYLE_NO_HEADER, "style should be no header");
    ASSERT_TRUE(out_config.delimiter == '\t', "delimiter should be tab");
    
    csv_formatter_destroy(formatter);
    PASS();
}

/* ============================================================================
 * Field Escaping Tests
 * ============================================================================ */

/**
 * Test field that doesn't need escaping.
 */
static void test_escape_simple_field(void) {
    TEST(escape_simple_field);
    
    csv_config_t config;
    csv_config_init(&config);
    
    char buffer[256];
    size_t len = csv_escape_field("hello", buffer, sizeof(buffer), &config);
    
    ASSERT_STR_EQUALS(buffer, "hello", "simple field should not be escaped");
    ASSERT_TRUE(len == 5, "length should be 5");
    
    PASS();
}

/**
 * Test field containing comma.
 */
static void test_escape_field_with_comma(void) {
    TEST(escape_field_with_comma);
    
    csv_config_t config;
    csv_config_init(&config);
    
    char buffer[256];
    csv_escape_field("hello,world", buffer, sizeof(buffer), &config);
    
    ASSERT_STR_EQUALS(buffer, "\"hello,world\"", "field with comma should be quoted");
    
    PASS();
}

/**
 * Test field containing quote.
 */
static void test_escape_field_with_quote(void) {
    TEST(escape_field_with_quote);
    
    csv_config_t config;
    csv_config_init(&config);
    
    char buffer[256];
    csv_escape_field("say \"hello\"", buffer, sizeof(buffer), &config);
    
    ASSERT_STR_EQUALS(buffer, "\"say \"\"hello\"\"\"", 
                      "field with quote should have doubled quotes");
    
    PASS();
}

/**
 * Test field containing newline.
 */
static void test_escape_field_with_newline(void) {
    TEST(escape_field_with_newline);
    
    csv_config_t config;
    csv_config_init(&config);
    
    char buffer[256];
    csv_escape_field("line1\nline2", buffer, sizeof(buffer), &config);
    
    ASSERT_STR_EQUALS(buffer, "\"line1\nline2\"", 
                      "field with newline should be quoted");
    
    PASS();
}

/**
 * Test field containing carriage return.
 */
static void test_escape_field_with_cr(void) {
    TEST(escape_field_with_cr);
    
    csv_config_t config;
    csv_config_init(&config);
    
    char buffer[256];
    csv_escape_field("line1\rline2", buffer, sizeof(buffer), &config);
    
    ASSERT_STR_EQUALS(buffer, "\"line1\rline2\"", 
                      "field with CR should be quoted");
    
    PASS();
}

/**
 * Test quote_all_fields option.
 */
static void test_escape_quote_all_fields(void) {
    TEST(escape_quote_all_fields);
    
    csv_config_t config;
    csv_config_init(&config);
    config.quote_all_fields = true;
    
    char buffer[256];
    csv_escape_field("simple", buffer, sizeof(buffer), &config);
    
    ASSERT_STR_EQUALS(buffer, "\"simple\"", 
                      "simple field should be quoted when quote_all_fields is true");
    
    PASS();
}

/**
 * Test csv_escape_field_alloc.
 */
static void test_escape_field_alloc(void) {
    TEST(escape_field_alloc);
    
    csv_config_t config;
    csv_config_init(&config);
    
    char* escaped = csv_escape_field_alloc("hello,\"world\"", &config);
    ASSERT_NOT_NULL(escaped, "escaped string should be allocated");
    ASSERT_STR_EQUALS(escaped, "\"hello,\"\"world\"\"\"", 
                      "complex field should be properly escaped");
    
    free(escaped);
    PASS();
}

/**
 * Test escaping NULL input.
 */
static void test_escape_null_input(void) {
    TEST(escape_null_input);
    
    csv_config_t config;
    csv_config_init(&config);
    
    char buffer[256];
    size_t len = csv_escape_field(NULL, buffer, sizeof(buffer), &config);
    
    ASSERT_STR_EQUALS(buffer, "", "NULL input should produce empty string");
    ASSERT_TRUE(len == 0, "length should be 0");
    
    char* allocated = csv_escape_field_alloc(NULL, &config);
    ASSERT_NOT_NULL(allocated, "should return empty string");
    ASSERT_STR_EQUALS(allocated, "", "NULL input should produce empty string");
    free(allocated);
    
    PASS();
}

/**
 * Test custom delimiter escaping.
 */
static void test_escape_custom_delimiter(void) {
    TEST(escape_custom_delimiter);
    
    csv_config_t config;
    csv_config_init(&config);
    config.delimiter = '\t';
    
    char buffer[256];
    csv_escape_field("hello\tworld", buffer, sizeof(buffer), &config);
    
    ASSERT_STR_EQUALS(buffer, "\"hello\tworld\"", 
                      "field with custom delimiter should be quoted");
    
    /* Comma should not trigger escaping with tab delimiter */
    csv_escape_field("hello,world", buffer, sizeof(buffer), &config);
    ASSERT_STR_EQUALS(buffer, "hello,world", 
                      "comma should not be escaped with tab delimiter");
    
    PASS();
}

/**
 * Test csv_field_needs_escaping.
 */
static void test_field_needs_escaping(void) {
    TEST(field_needs_escaping);
    
    csv_config_t config;
    csv_config_init(&config);
    
    ASSERT_FALSE(csv_field_needs_escaping("simple", &config), 
                 "simple field should not need escaping");
    ASSERT_TRUE(csv_field_needs_escaping("has,comma", &config), 
                "field with comma should need escaping");
    ASSERT_TRUE(csv_field_needs_escaping("has\"quote", &config), 
                "field with quote should need escaping");
    ASSERT_TRUE(csv_field_needs_escaping("has\nnewline", &config), 
                "field with newline should need escaping");
    ASSERT_TRUE(csv_field_needs_escaping("has\rcarriage", &config), 
                "field with CR should need escaping");
    ASSERT_FALSE(csv_field_needs_escaping(NULL, &config), 
                 "NULL field should not need escaping");
    
    PASS();
}

/* ============================================================================
 * Header Generation Tests
 * ============================================================================ */

/**
 * Test header generation with default columns.
 */
static void test_header_generation_default(void) {
    TEST(header_generation_default);
    
    csv_formatter_t* formatter = csv_formatter_create();
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    char* header = csv_generate_header(formatter);
    ASSERT_NOT_NULL(header, "header should be generated");
    
    /* Default columns: address,size,timestamp,function,file,line */
    ASSERT_STR_CONTAINS(header, "address", "header should contain address");
    ASSERT_STR_CONTAINS(header, "size", "header should contain size");
    ASSERT_STR_CONTAINS(header, "timestamp", "header should contain timestamp");
    ASSERT_STR_CONTAINS(header, "function", "header should contain function");
    ASSERT_STR_CONTAINS(header, "file", "header should contain file");
    ASSERT_STR_CONTAINS(header, "line", "header should contain line");
    ASSERT_STR_CONTAINS(header, "\r\n", "header should end with CRLF");
    
    free(header);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test header generation with all columns.
 */
static void test_header_generation_all_columns(void) {
    TEST(header_generation_all_columns);
    
    csv_config_t config;
    csv_config_init(&config);
    config.columns = CSV_COL_ALL;
    
    csv_formatter_t* formatter = csv_formatter_create_with_config(&config);
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    char* header = csv_generate_header(formatter);
    ASSERT_NOT_NULL(header, "header should be generated");
    
    ASSERT_STR_CONTAINS(header, "group_id", "header should contain group_id");
    ASSERT_STR_CONTAINS(header, "total_in_group", "header should contain total_in_group");
    ASSERT_STR_CONTAINS(header, "total_bytes", "header should contain total_bytes");
    ASSERT_STR_CONTAINS(header, "backtrace", "header should contain backtrace");
    
    free(header);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test column name function.
 */
static void test_column_name(void) {
    TEST(column_name);
    
    ASSERT_STR_EQUALS(csv_column_name(CSV_COL_ADDRESS), "address", 
                      "CSV_COL_ADDRESS should return 'address'");
    ASSERT_STR_EQUALS(csv_column_name(CSV_COL_SIZE), "size", 
                      "CSV_COL_SIZE should return 'size'");
    ASSERT_STR_EQUALS(csv_column_name(CSV_COL_TIMESTAMP), "timestamp", 
                      "CSV_COL_TIMESTAMP should return 'timestamp'");
    ASSERT_STR_EQUALS(csv_column_name(CSV_COL_FUNCTION), "function", 
                      "CSV_COL_FUNCTION should return 'function'");
    ASSERT_STR_EQUALS(csv_column_name(CSV_COL_FILE), "file", 
                      "CSV_COL_FILE should return 'file'");
    ASSERT_STR_EQUALS(csv_column_name(CSV_COL_LINE), "line", 
                      "CSV_COL_LINE should return 'line'");
    ASSERT_STR_EQUALS(csv_column_name(CSV_COL_GROUP_ID), "group_id", 
                      "CSV_COL_GROUP_ID should return 'group_id'");
    ASSERT_NULL(csv_column_name(CSV_COL_NONE), "CSV_COL_NONE should return NULL");
    
    PASS();
}

/* ============================================================================
 * Format Address/Timestamp Tests
 * ============================================================================ */

/**
 * Test address formatting.
 */
static void test_format_address(void) {
    TEST(format_address);
    
    char buffer[64];
    char* result = csv_format_address((void*)0x12345678, buffer, sizeof(buffer));
    
    ASSERT_NOT_NULL(result, "result should not be NULL");
    ASSERT_STR_CONTAINS(buffer, "0x", "address should start with 0x");
    ASSERT_STR_CONTAINS(buffer, "12345678", "address should contain hex value");
    
    PASS();
}

/**
 * Test timestamp formatting.
 */
static void test_format_timestamp(void) {
    TEST(format_timestamp);
    
    char buffer[64];
    /* Test with a known timestamp: 2024-01-15 12:00:00 UTC = 1705320000 seconds */
    uint64_t timestamp = 1705320000ULL * 1000000;  /* In microseconds */
    
    char* result = csv_format_timestamp(timestamp, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(result, "result should not be NULL");
    ASSERT_STR_CONTAINS(buffer, "2024-01-15", "timestamp should contain date");
    ASSERT_STR_CONTAINS(buffer, "12:00:00", "timestamp should contain time");
    
    PASS();
}

/* ============================================================================
 * Report Generation Tests
 * ============================================================================ */

/**
 * Test report_to_csv with empty report.
 */
static void test_report_empty(void) {
    TEST(report_empty);
    
    csv_formatter_t* formatter = csv_formatter_create();
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    leak_report_t* report = leak_report_create();
    ASSERT_NOT_NULL(report, "report should be created");
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV should be generated");
    
    /* Should just have header row */
    ASSERT_STR_CONTAINS(csv, "address", "CSV should contain header");
    
    free(csv);
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test report_to_csv with NULL parameters.
 */
static void test_report_null_params(void) {
    TEST(report_null_params);
    
    csv_formatter_t* formatter = csv_formatter_create();
    leak_report_t* report = leak_report_create();
    
    ASSERT_NULL(report_to_csv(NULL, report), "NULL formatter should return NULL");
    ASSERT_NULL(report_to_csv(formatter, NULL), "NULL report should return NULL");
    
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test report_to_csv with single entry.
 */
static void test_report_single_entry(void) {
    TEST(report_single_entry);
    
    csv_formatter_t* formatter = csv_formatter_create();
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    leak_report_t* report = leak_report_create();
    ASSERT_NOT_NULL(report, "report should be created");
    
    /* Create a leak group with one entry */
    leak_group_t* group = leak_group_create("test.c", 42, true);
    ASSERT_NOT_NULL(group, "group should be created");
    
    leak_entry_t* entry = leak_entry_create((void*)0x1000, 64, "test.c", 42, true);
    ASSERT_NOT_NULL(entry, "entry should be created");
    entry->timestamp = 1705320000ULL * 1000000;
    
    leak_group_add_entry(group, entry);
    leak_report_add_group(report, group);
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV should be generated");
    
    /* Should have header and one data row */
    ASSERT_STR_CONTAINS(csv, "address", "CSV should contain header");
    ASSERT_STR_CONTAINS(csv, "0x", "CSV should contain address");
    ASSERT_STR_CONTAINS(csv, "64", "CSV should contain size");
    ASSERT_STR_CONTAINS(csv, "test.c", "CSV should contain file");
    ASSERT_STR_CONTAINS(csv, "42", "CSV should contain line");
    
    free(csv);
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test report with no header.
 */
static void test_report_no_header(void) {
    TEST(report_no_header);
    
    csv_config_t config;
    csv_config_init(&config);
    config.style = CSV_STYLE_NO_HEADER;
    
    csv_formatter_t* formatter = csv_formatter_create_with_config(&config);
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    leak_report_t* report = leak_report_create();
    ASSERT_NOT_NULL(report, "report should be created");
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV should be generated");
    
    /* Empty report with no header should be empty string */
    ASSERT_STR_EQUALS(csv, "", "CSV should be empty for empty report without header");
    
    free(csv);
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test report with custom delimiter.
 */
static void test_report_custom_delimiter(void) {
    TEST(report_custom_delimiter);
    
    csv_config_t config;
    csv_config_init(&config);
    config.delimiter = ';';
    
    csv_formatter_t* formatter = csv_formatter_create_with_config(&config);
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    leak_report_t* report = leak_report_create();
    leak_group_t* group = leak_group_create("test.c", 1, true);
    leak_entry_t* entry = leak_entry_create((void*)0x1000, 32, "test.c", 1, true);
    leak_group_add_entry(group, entry);
    leak_report_add_group(report, group);
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV should be generated");
    
    /* Check header uses semicolon */
    ASSERT_STR_CONTAINS(csv, "address;size", "header should use semicolon delimiter");
    
    free(csv);
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test report with summary row.
 */
static void test_report_with_summary(void) {
    TEST(report_with_summary);
    
    csv_config_t config;
    csv_config_init(&config);
    config.include_summary_row = true;
    
    csv_formatter_t* formatter = csv_formatter_create_with_config(&config);
    ASSERT_NOT_NULL(formatter, "formatter should be created");
    
    leak_report_t* report = leak_report_create();
    leak_group_t* group = leak_group_create("test.c", 1, true);
    leak_entry_t* entry = leak_entry_create((void*)0x1000, 128, "test.c", 1, true);
    leak_group_add_entry(group, entry);
    leak_report_add_group(report, group);
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV should be generated");
    
    /* Check for TOTAL marker in summary row */
    ASSERT_STR_CONTAINS(csv, "TOTAL", "CSV should contain summary row with TOTAL");
    ASSERT_STR_CONTAINS(csv, "128", "CSV should contain total bytes");
    
    free(csv);
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test file output.
 */
static void test_write_to_file(void) {
    TEST(write_to_file);
    
    csv_formatter_t* formatter = csv_formatter_create();
    leak_report_t* report = leak_report_create();
    leak_group_t* group = leak_group_create("test.c", 1, true);
    leak_entry_t* entry = leak_entry_create((void*)0x1000, 32, "test.c", 1, true);
    leak_group_add_entry(group, entry);
    leak_report_add_group(report, group);
    
    const char* filepath = "/tmp/test_memrogue_csv.csv";
    bool success = csv_write_to_file(formatter, report, filepath);
    ASSERT_TRUE(success, "file write should succeed");
    
    /* Verify file exists and has content */
    FILE* f = fopen(filepath, "r");
    ASSERT_NOT_NULL(f, "file should exist");
    
    char buffer[1024];
    char* content = fgets(buffer, sizeof(buffer), f);
    fclose(f);
    remove(filepath);
    
    ASSERT_NOT_NULL(content, "file should have content");
    ASSERT_STR_CONTAINS(buffer, "address", "file should contain header");
    
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test stream output.
 */
static void test_write_to_stream(void) {
    TEST(write_to_stream);
    
    csv_formatter_t* formatter = csv_formatter_create();
    leak_report_t* report = leak_report_create();
    
    const char* filepath = "/tmp/test_memrogue_csv_stream.csv";
    FILE* stream = fopen(filepath, "w");
    ASSERT_NOT_NULL(stream, "stream should be opened");
    
    int written = csv_write_to_stream(formatter, report, stream);
    fclose(stream);
    
    ASSERT_TRUE(written >= 0, "write should succeed");
    
    remove(filepath);
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/**
 * Test max_entries_per_group limit.
 */
static void test_max_entries_per_group(void) {
    TEST(max_entries_per_group);
    
    csv_config_t config;
    csv_config_init(&config);
    config.max_entries_per_group = 2;
    config.style = CSV_STYLE_NO_HEADER;  /* Easier to count rows */
    
    csv_formatter_t* formatter = csv_formatter_create_with_config(&config);
    leak_report_t* report = leak_report_create();
    leak_group_t* group = leak_group_create("test.c", 1, true);
    
    /* Add 5 entries */
    for (int i = 0; i < 5; i++) {
        leak_entry_t* entry = leak_entry_create((void*)(uintptr_t)(0x1000 + i * 0x100), 
                                                 32, "test.c", i + 1, true);
        leak_group_add_entry(group, entry);
    }
    leak_report_add_group(report, group);
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV should be generated");
    
    /* Count CRLF occurrences to determine row count */
    int row_count = 0;
    const char* p = csv;
    while ((p = strstr(p, "\r\n")) != NULL) {
        row_count++;
        p += 2;
    }
    
    /* Should have exactly 2 rows (limited by max_entries_per_group) */
    ASSERT_TRUE(row_count == 2, "should have only 2 data rows");
    
    free(csv);
    leak_report_destroy(report);
    csv_formatter_destroy(formatter);
    PASS();
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("=== CSV Formatter Tests ===\n\n");
    
    printf("Configuration Tests:\n");
    test_config_init_defaults();
    test_config_init_null();
    
    printf("\nFormatter Lifecycle Tests:\n");
    test_formatter_create_default();
    test_formatter_create_with_config();
    test_formatter_create_null_config();
    test_formatter_destroy_null();
    test_formatter_config_update();
    
    printf("\nField Escaping Tests:\n");
    test_escape_simple_field();
    test_escape_field_with_comma();
    test_escape_field_with_quote();
    test_escape_field_with_newline();
    test_escape_field_with_cr();
    test_escape_quote_all_fields();
    test_escape_field_alloc();
    test_escape_null_input();
    test_escape_custom_delimiter();
    test_field_needs_escaping();
    
    printf("\nHeader Generation Tests:\n");
    test_header_generation_default();
    test_header_generation_all_columns();
    test_column_name();
    
    printf("\nFormat Utilities Tests:\n");
    test_format_address();
    test_format_timestamp();
    
    printf("\nReport Generation Tests:\n");
    test_report_empty();
    test_report_null_params();
    test_report_single_entry();
    test_report_no_header();
    test_report_custom_delimiter();
    test_report_with_summary();
    test_write_to_file();
    test_write_to_stream();
    test_max_entries_per_group();
    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
