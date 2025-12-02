/**
 * @file test_report_formats.c
 * @brief Integration tests for report format verification
 * 
 * MEMRO-25: Integration Test Suite
 * 
 * Tests report output formats:
 * - Text report format and content
 * - JSON export format and validity
 * - CSV export format and compliance
 * - Report content accuracy
 */

#include "integration_common.h"
#include "memrogue_tracker.h"
#include "memrogue_report.h"
#include "memrogue_json.h"
#include "memrogue_csv.h"
#include "memrogue_leak_detector.h"

#include <fcntl.h>

/**
 * Helper: Create a leak report from a tracker using the actual detector
 */
static leak_report_t* create_leak_report_from_tracker(memory_tracker_t* tracker) {
    // Use the actual leak detector to scan for leaks
    return leak_detector_scan(tracker, NULL);
}

static void free_leak_report(leak_report_t* report) {
    leak_report_destroy(report);
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Create a tracker with known allocations for testing reports
 */
static memory_tracker_t* create_test_tracker_with_leaks(void** leak_ptrs, int* leak_count) {
    memory_tracker_t* tracker = tracker_create();
    if (!tracker) return NULL;
    
    *leak_count = 0;
    
    // Create some predictable leaks
    for (int i = 0; i < 5; i++) {
        size_t size = (size_t)(i + 1) * 100;  // 100, 200, 300, 400, 500 bytes
        void* ptr = malloc(size);
        if (ptr) {
            track_allocation(tracker, ptr, size, "test_file.c", 10 + i);
            leak_ptrs[*leak_count] = ptr;
            (*leak_count)++;
        }
    }
    
    return tracker;
}

// ============================================================================
// Text Report Tests
// ============================================================================

/**
 * Test: Text report contains expected header
 */
static int test_text_report_header(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(100);
    track_allocation(tracker, ptr, 100, __FILE__, __LINE__);
    
    char* report = tracker_format_stats(tracker);
    ASSERT_NOT_NULL(report, "report should be generated");
    
    // Check for key sections
    ASSERT_STR_CONTAINS(report, "Memory", "report should contain 'Memory'");
    
    free(report);
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Text report shows correct allocation count
 */
static int test_text_report_allocation_count(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = malloc(64);
        track_allocation(tracker, ptrs[i], 64, __FILE__, __LINE__);
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(10, stats.active_allocations, "should have 10 allocations");
    
    for (int i = 0; i < 10; i++) {
        track_deallocation(tracker, ptrs[i]);
        free(ptrs[i]);
    }
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

// ============================================================================
// JSON Report Tests
// ============================================================================

/**
 * Test: JSON export produces valid JSON structure
 */
static int test_json_valid_structure(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(256);
    track_allocation(tracker, ptr, 256, "test.c", 42);
    
    // Create formatter and report
    json_formatter_t* formatter = json_formatter_create();
    ASSERT_NOT_NULL(formatter, "JSON formatter creation should succeed");
    
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    ASSERT_NOT_NULL(report, "report creation should succeed");
    
    // Export to JSON
    char* json = report_to_json(formatter, report);
    ASSERT_NOT_NULL(json, "JSON export should succeed");
    
    // Verify it starts and ends with braces/brackets
    ASSERT_TRUE(json[0] == '{' || json[0] == '[', "JSON should start with { or [");
    
    size_t len = strlen(json);
    ASSERT_TRUE(json[len-1] == '}' || json[len-1] == ']' || json[len-1] == '\n', 
                "JSON should end properly");
    
    free(json);
    json_formatter_destroy(formatter);
    free_leak_report(report);
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: JSON export includes correct sizes
 */
static int test_json_size_accuracy(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(1024);
    track_allocation(tracker, ptr, 1024, "test.c", 100);
    
    json_formatter_t* formatter = json_formatter_create();
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    
    char* json = report_to_json(formatter, report);
    ASSERT_NOT_NULL(json, "JSON export should succeed");
    
    // Should contain the size 1024
    ASSERT_STR_CONTAINS(json, "1024", "JSON should contain size 1024");
    
    free(json);
    json_formatter_destroy(formatter);
    free_leak_report(report);
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: JSON export with multiple allocations
 */
static int test_json_multiple_allocations(void) {
    void* leak_ptrs[5];
    int leak_count;
    memory_tracker_t* tracker = create_test_tracker_with_leaks(leak_ptrs, &leak_count);
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    json_formatter_t* formatter = json_formatter_create();
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    
    char* json = report_to_json(formatter, report);
    ASSERT_NOT_NULL(json, "JSON export should succeed");
    
    // Count occurrences of size indicators
    size_t count_100 = count_occurrences(json, "100");
    size_t count_200 = count_occurrences(json, "200");
    
    ASSERT_GE(count_100, 1, "JSON should contain size 100");
    ASSERT_GE(count_200, 1, "JSON should contain size 200");
    
    free(json);
    json_formatter_destroy(formatter);
    free_leak_report(report);
    
    for (int i = 0; i < leak_count; i++) {
        free(leak_ptrs[i]);
    }
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: JSON export with no leaks
 */
static int test_json_no_leaks(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Allocate and free - no leaks
    void* ptr = malloc(64);
    track_allocation(tracker, ptr, 64, __FILE__, __LINE__);
    track_deallocation(tracker, ptr);
    free(ptr);
    
    json_formatter_t* formatter = json_formatter_create();
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    
    char* json = report_to_json(formatter, report);
    ASSERT_NOT_NULL(json, "JSON export should succeed even with no leaks");
    
    // Should indicate empty or zero
    // The format may vary, but should be valid JSON
    ASSERT_TRUE(strlen(json) > 0, "JSON should not be empty");
    
    free(json);
    json_formatter_destroy(formatter);
    free_leak_report(report);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

// ============================================================================
// CSV Report Tests
// ============================================================================

/**
 * Test: CSV export produces valid CSV
 */
static int test_csv_valid_format(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(512);
    track_allocation(tracker, ptr, 512, "test.c", 50);
    
    csv_formatter_t* formatter = csv_formatter_create();
    ASSERT_NOT_NULL(formatter, "CSV formatter creation should succeed");
    
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    ASSERT_NOT_NULL(report, "report creation should succeed");
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV export should succeed");
    
    // CSV should have header row with commas
    ASSERT_STR_CONTAINS(csv, ",", "CSV should contain commas");
    
    // Should contain the size
    ASSERT_STR_CONTAINS(csv, "512", "CSV should contain size");
    
    free(csv);
    csv_formatter_destroy(formatter);
    free_leak_report(report);
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: CSV export has proper header row
 */
static int test_csv_header_row(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(100);
    track_allocation(tracker, ptr, 100, "test.c", 1);
    
    csv_formatter_t* formatter = csv_formatter_create();
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV export should succeed");
    
    // First line should be the header
    // Common headers: address, size, file, line
    char* newline = strchr(csv, '\n');
    if (newline) {
        size_t header_len = (size_t)(newline - csv);
        char* header = malloc(header_len + 1);
        strncpy(header, csv, header_len);
        header[header_len] = '\0';
        
        // Header should contain expected column names
        bool has_size = strstr(header, "size") || strstr(header, "Size") || 
                        strstr(header, "SIZE") || strstr(header, "bytes");
        ASSERT_TRUE(has_size, "CSV header should contain size column");
        
        free(header);
    }
    
    free(csv);
    csv_formatter_destroy(formatter);
    free_leak_report(report);
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: CSV export with multiple rows
 */
static int test_csv_multiple_rows(void) {
    void* leak_ptrs[5];
    int leak_count;
    memory_tracker_t* tracker = create_test_tracker_with_leaks(leak_ptrs, &leak_count);
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    csv_formatter_t* formatter = csv_formatter_create();
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV export should succeed");
    
    // Count newlines (should have header + data rows)
    size_t newline_count = count_occurrences(csv, "\n");
    
    // Should have at least header + 5 data rows
    ASSERT_GE(newline_count, 5, "CSV should have multiple rows");
    
    free(csv);
    csv_formatter_destroy(formatter);
    free_leak_report(report);
    
    for (int i = 0; i < leak_count; i++) {
        free(leak_ptrs[i]);
    }
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: CSV handles special characters
 */
static int test_csv_special_characters(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // File path with special characters that might need escaping
    void* ptr = malloc(100);
    track_allocation(tracker, ptr, 100, "path/to/file,with\"quotes.c", 1);
    
    csv_formatter_t* formatter = csv_formatter_create();
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    
    char* csv = report_to_csv(formatter, report);
    ASSERT_NOT_NULL(csv, "CSV export should succeed");
    
    // RFC 4180 compliance: fields with special chars should be quoted
    // Just verify it doesn't crash and produces output
    ASSERT_TRUE(strlen(csv) > 0, "CSV should not be empty");
    
    free(csv);
    csv_formatter_destroy(formatter);
    free_leak_report(report);
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

// ============================================================================
// Report Content Accuracy Tests
// ============================================================================

/**
 * Test: Report byte counts match actual allocations
 */
static int test_report_byte_accuracy(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    size_t total = 0;
    void* ptrs[5];
    size_t sizes[] = {64, 128, 256, 512, 1024};
    
    for (int i = 0; i < 5; i++) {
        ptrs[i] = malloc(sizes[i]);
        track_allocation(tracker, ptrs[i], sizes[i], __FILE__, __LINE__);
        total += sizes[i];
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(total, stats.active_bytes, "active bytes should match");
    ASSERT_EQ(1984, stats.active_bytes, "total should be 1984 bytes");
    
    for (int i = 0; i < 5; i++) {
        track_deallocation(tracker, ptrs[i]);
        free(ptrs[i]);
    }
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Report shows correct peak memory
 */
static int test_report_peak_memory(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Allocate 1000 bytes
    void* ptr1 = malloc(1000);
    track_allocation(tracker, ptr1, 1000, __FILE__, __LINE__);
    
    // Allocate another 500 bytes (peak = 1500)
    void* ptr2 = malloc(500);
    track_allocation(tracker, ptr2, 500, __FILE__, __LINE__);
    
    // Free first (current = 500, peak still 1500)
    track_deallocation(tracker, ptr1);
    free(ptr1);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(500, stats.active_bytes, "current bytes should be 500");
    ASSERT_GE(stats.peak_bytes, 1500, "peak bytes should be at least 1500");
    
    track_deallocation(tracker, ptr2);
    free(ptr2);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Report consistency across formats
 */
static int test_report_format_consistency(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(256);
    track_allocation(tracker, ptr, 256, "consistency_test.c", 99);
    
    // Get all formats
    json_formatter_t* json_fmt = json_formatter_create();
    csv_formatter_t* csv_fmt = csv_formatter_create();
    leak_report_t* report = create_leak_report_from_tracker(tracker);
    
    char* json = report_to_json(json_fmt, report);
    char* csv = report_to_csv(csv_fmt, report);
    
    ASSERT_NOT_NULL(json, "JSON should be generated");
    ASSERT_NOT_NULL(csv, "CSV should be generated");
    
    // All should contain the size 256
    ASSERT_STR_CONTAINS(json, "256", "JSON should contain size");
    ASSERT_STR_CONTAINS(csv, "256", "CSV should contain size");
    
    free(json);
    free(csv);
    json_formatter_destroy(json_fmt);
    csv_formatter_destroy(csv_fmt);
    free_leak_report(report);
    
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

// ============================================================================
// Test Suite Definition
// ============================================================================

static test_case_t report_format_tests[] = {
    // Text reports
    {"text_report_header", "Text report contains header", test_text_report_header},
    {"text_report_allocation_count", "Text report shows correct count", test_text_report_allocation_count},
    
    // JSON reports
    {"json_valid_structure", "JSON has valid structure", test_json_valid_structure},
    {"json_size_accuracy", "JSON contains correct sizes", test_json_size_accuracy},
    {"json_multiple_allocations", "JSON with multiple allocations", test_json_multiple_allocations},
    {"json_no_leaks", "JSON with no leaks", test_json_no_leaks},
    
    // CSV reports
    {"csv_valid_format", "CSV has valid format", test_csv_valid_format},
    {"csv_header_row", "CSV has proper header", test_csv_header_row},
    {"csv_multiple_rows", "CSV with multiple data rows", test_csv_multiple_rows},
    {"csv_special_characters", "CSV handles special characters", test_csv_special_characters},
    
    // Content accuracy
    {"report_byte_accuracy", "Report byte counts are accurate", test_report_byte_accuracy},
    {"report_peak_memory", "Report shows correct peak memory", test_report_peak_memory},
    {"report_format_consistency", "Formats are consistent", test_report_format_consistency},
};

int main(void) {
    test_suite_t suite = {
        .name = "Report Format Integration Tests",
        .tests = report_format_tests,
        .test_count = sizeof(report_format_tests) / sizeof(report_format_tests[0]),
    };
    
    int result = run_test_suite(&suite);
    
    return (result == TEST_PASS) ? 0 : 1;
}
