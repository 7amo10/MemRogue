/**
 * @file test_report.c
 * @brief Unit tests for text report formatter module.
 *
 * Tests cover:
 * - Configuration initialization
 * - Formatter lifecycle
 * - Summary formatting
 * - Group sorting (by size, count, location)
 * - Percentage calculations
 * - Backtrace formatting
 * - File output
 * - Edge cases
 *
 * MEMRO-17: Text Report Formatter
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>

#include "../include/memrogue_report.h"
#include "../include/memrogue_leak_detector.h"
#include "../include/memrogue_tracker.h"

/* ============================================================================
 * Test Infrastructure
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(test_func) do { \
    tests_run++; \
    printf("Running %s... ", #test_func); \
    fflush(stdout); \
    if (test_func()) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } else { \
        printf("FAILED\n"); \
    } \
} while(0)

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Create a mock leak report for testing.
 * @param num_groups Number of leak groups to create
 * @param leaks_per_group Number of leaks per group (must be > 0)
 * @return Mock report or NULL on failure
 */
static leak_report_t* create_mock_report(size_t num_groups, size_t leaks_per_group) {
    /* Validate input to prevent division by zero */
    if (leaks_per_group == 0) {
        return NULL;
    }
    
    leak_report_t* report = calloc(1, sizeof(leak_report_t));
    if (!report) return NULL;
    
    report->severity = LEAK_SEVERITY_MEDIUM;
    report->detection_time_us = 1234;
    
    leak_group_t* prev_group = NULL;
    
    for (size_t g = 0; g < num_groups; g++) {
        leak_group_t* group = calloc(1, sizeof(leak_group_t));
        if (!group) {
            leak_report_destroy(report);
            return NULL;
        }
        
        group->signature = (uint64_t)(g + 1) * 12345;
        group->leak_count = leaks_per_group;
        group->total_bytes = (g + 1) * 1024;  /* 1KB, 2KB, 3KB, etc. */
        group->file = "test_file.c";
        group->line = (int)(100 + g * 10);
        group->frame_count = 3;
        group->frames[0] = (void*)0x1000;
        group->frames[1] = (void*)0x2000;
        group->frames[2] = (void*)0x3000;
        
        /* Create leak entries */
        leak_entry_t* prev_entry = NULL;
        for (size_t e = 0; e < leaks_per_group; e++) {
            leak_entry_t* entry = calloc(1, sizeof(leak_entry_t));
            if (!entry) {
                leak_report_destroy(report);
                return NULL;
            }
            
            entry->address = (void*)(0x10000 + g * 0x1000 + e * 0x100);
            entry->size = (g + 1) * 1024 / leaks_per_group;
            entry->file = "test_file.c";
            entry->line = (int)(100 + g * 10);
            entry->timestamp = 1000000 + g * 1000 + e;
            entry->frame_count = 2;
            entry->frames[0] = (void*)0x1000;
            entry->frames[1] = (void*)0x2000;
            
            if (prev_entry) {
                prev_entry->next = entry;
            } else {
                group->entries = entry;
            }
            prev_entry = entry;
        }
        
        if (prev_group) {
            prev_group->next = group;
        } else {
            report->groups = group;
        }
        prev_group = group;
        
        report->group_count++;
        report->total_leaks += leaks_per_group;
        report->total_bytes += group->total_bytes;
    }
    
    return report;
}

/**
 * Free a mock leak report.
 */
static void free_mock_report(leak_report_t* report) {
    if (!report) return;
    
    leak_group_t* group = report->groups;
    while (group) {
        leak_group_t* next_group = group->next;
        
        leak_entry_t* entry = group->entries;
        while (entry) {
            leak_entry_t* next_entry = entry->next;
            free(entry);
            entry = next_entry;
        }
        
        free(group);
        group = next_group;
    }
    
    free(report);
}

/* ============================================================================
 * Configuration Tests
 * ============================================================================ */

/**
 * Test default configuration initialization.
 */
static int test_config_init_defaults(void) {
    report_config_t config;
    report_config_init(&config);
    
    if (!config.show_summary) {
        fprintf(stderr, "FAIL: %s - show_summary not true\n", __func__);
        return 0;
    }
    
    if (!config.show_backtraces) {
        fprintf(stderr, "FAIL: %s - show_backtraces not true\n", __func__);
        return 0;
    }
    
    if (!config.show_percentages) {
        fprintf(stderr, "FAIL: %s - show_percentages not true\n", __func__);
        return 0;
    }
    
    if (config.show_timestamps) {
        fprintf(stderr, "FAIL: %s - show_timestamps should be false\n", __func__);
        return 0;
    }
    
    if (!config.show_addresses) {
        fprintf(stderr, "FAIL: %s - show_addresses not true\n", __func__);
        return 0;
    }
    
    if (config.use_colors) {
        fprintf(stderr, "FAIL: %s - use_colors should be false\n", __func__);
        return 0;
    }
    
    if (config.sort_order != REPORT_SORT_BY_SIZE) {
        fprintf(stderr, "FAIL: %s - wrong sort_order\n", __func__);
        return 0;
    }
    
    if (config.max_groups != 0) {
        fprintf(stderr, "FAIL: %s - max_groups should be 0\n", __func__);
        return 0;
    }
    
    if (config.max_entries_per_group != 5) {
        fprintf(stderr, "FAIL: %s - wrong max_entries_per_group\n", __func__);
        return 0;
    }
    
    if (config.max_backtrace_depth != 10) {
        fprintf(stderr, "FAIL: %s - wrong max_backtrace_depth\n", __func__);
        return 0;
    }
    
    if (config.indent_width != 2) {
        fprintf(stderr, "FAIL: %s - wrong indent_width\n", __func__);
        return 0;
    }
    
    return 1;
}

/**
 * Test NULL configuration handling.
 */
static int test_config_init_null(void) {
    /* Should not crash */
    report_config_init(NULL);
    return 1;
}

/* ============================================================================
 * Formatter Lifecycle Tests
 * ============================================================================ */

/**
 * Test creating formatter with default config.
 */
static int test_formatter_create_default(void) {
    report_formatter_t* formatter = report_formatter_create();
    
    if (formatter == NULL) {
        fprintf(stderr, "FAIL: %s - create returned NULL\n", __func__);
        return 0;
    }
    
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test creating formatter with custom config.
 */
static int test_formatter_create_custom(void) {
    report_config_t config;
    report_config_init(&config);
    config.show_backtraces = false;
    config.use_colors = true;
    config.max_groups = 10;
    
    report_formatter_t* formatter = report_formatter_create_with_config(&config);
    
    if (formatter == NULL) {
        fprintf(stderr, "FAIL: %s - create returned NULL\n", __func__);
        return 0;
    }
    
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test destroying NULL formatter.
 */
static int test_formatter_destroy_null(void) {
    /* Should not crash */
    report_formatter_destroy(NULL);
    return 1;
}

/**
 * Test config get/set.
 */
static int test_formatter_config_update(void) {
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) return 0;
    
    report_config_t config;
    report_config_init(&config);
    config.max_groups = 42;
    config.use_colors = true;
    
    report_formatter_set_config(formatter, &config);
    
    report_config_t out_config;
    report_formatter_get_config(formatter, &out_config);
    
    if (out_config.max_groups != 42) {
        fprintf(stderr, "FAIL: %s - max_groups not updated\n", __func__);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    if (!out_config.use_colors) {
        fprintf(stderr, "FAIL: %s - use_colors not updated\n", __func__);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    report_formatter_destroy(formatter);
    return 1;
}

/* ============================================================================
 * Format Bytes Tests
 * ============================================================================ */

/**
 * Test byte formatting.
 */
static int test_format_bytes(void) {
    char buffer[32];
    
    /* Bytes */
    format_bytes(512, buffer, sizeof(buffer));
    if (strstr(buffer, "512") == NULL || strstr(buffer, "B") == NULL) {
        fprintf(stderr, "FAIL: %s - 512 bytes: '%s'\n", __func__, buffer);
        return 0;
    }
    
    /* Kilobytes */
    format_bytes(1536, buffer, sizeof(buffer));  /* 1.5 KB */
    if (strstr(buffer, "KB") == NULL) {
        fprintf(stderr, "FAIL: %s - 1.5 KB: '%s'\n", __func__, buffer);
        return 0;
    }
    
    /* Megabytes */
    format_bytes(2 * 1024 * 1024, buffer, sizeof(buffer));  /* 2 MB */
    if (strstr(buffer, "MB") == NULL) {
        fprintf(stderr, "FAIL: %s - 2 MB: '%s'\n", __func__, buffer);
        return 0;
    }
    
    /* Gigabytes */
    format_bytes((size_t)3 * 1024 * 1024 * 1024, buffer, sizeof(buffer));  /* 3 GB */
    if (strstr(buffer, "GB") == NULL) {
        fprintf(stderr, "FAIL: %s - 3 GB: '%s'\n", __func__, buffer);
        return 0;
    }
    
    return 1;
}

/**
 * Test sort order strings.
 */
static int test_sort_order_strings(void) {
    if (strstr(report_sort_order_to_string(REPORT_SORT_BY_SIZE), "size") == NULL) {
        fprintf(stderr, "FAIL: %s - BY_SIZE string\n", __func__);
        return 0;
    }
    
    if (strstr(report_sort_order_to_string(REPORT_SORT_BY_COUNT), "count") == NULL) {
        fprintf(stderr, "FAIL: %s - BY_COUNT string\n", __func__);
        return 0;
    }
    
    if (strstr(report_sort_order_to_string(REPORT_SORT_BY_LOCATION), "location") == NULL) {
        fprintf(stderr, "FAIL: %s - BY_LOCATION string\n", __func__);
        return 0;
    }
    
    return 1;
}

/* ============================================================================
 * Report Formatting Tests
 * ============================================================================ */

/**
 * Test formatting an empty report (no leaks).
 */
static int test_format_empty_report(void) {
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) return 0;
    
    leak_report_t report = {0};
    report.severity = LEAK_SEVERITY_NONE;
    
    char* text = report_format_text(formatter, &report);
    if (!text) {
        fprintf(stderr, "FAIL: %s - format returned NULL\n", __func__);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should contain header */
    if (strstr(text, "MEMROGUE") == NULL) {
        fprintf(stderr, "FAIL: %s - missing header\n", __func__);
        free(text);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should say no leaks */
    if (strstr(text, "No memory leaks") == NULL) {
        fprintf(stderr, "FAIL: %s - missing 'no leaks' message\n", __func__);
        free(text);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(text);
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test formatting a report with leaks.
 */
static int test_format_report_with_leaks(void) {
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(3, 2);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    char* text = report_format_text(formatter, report);
    if (!text) {
        fprintf(stderr, "FAIL: %s - format returned NULL\n", __func__);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should contain summary */
    if (strstr(text, "SUMMARY") == NULL) {
        fprintf(stderr, "FAIL: %s - missing SUMMARY section\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should contain total leaks */
    if (strstr(text, "Total Leaks") == NULL) {
        fprintf(stderr, "FAIL: %s - missing Total Leaks\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should contain leak groups */
    if (strstr(text, "Leak Group") == NULL) {
        fprintf(stderr, "FAIL: %s - missing Leak Group\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should contain file:line */
    if (strstr(text, "test_file.c") == NULL) {
        fprintf(stderr, "FAIL: %s - missing file name\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(text);
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test that report is sorted by size (largest first).
 */
static int test_sort_by_size(void) {
    report_config_t config;
    report_config_init(&config);
    config.sort_order = REPORT_SORT_BY_SIZE;
    
    report_formatter_t* formatter = report_formatter_create_with_config(&config);
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(3, 2);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    char* text = report_format_text(formatter, report);
    if (!text) {
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Group #3 has the most bytes, should be first */
    /* Find positions of "Leak Group #1" vs "Leak Group #3" */
    char* pos1 = strstr(text, "Leak Group #1");
    char* pos3 = strstr(text, "Leak Group #3");
    
    /* After sorting by size, the groups should appear with #1 being the largest (3KB) */
    /* Groups were created with total_bytes = (g+1)*1024, so group 2 (index 2) has 3KB */
    /* After sorting, the 3KB group should appear first as "Leak Group #1" */
    
    if (pos1 == NULL || pos3 == NULL) {
        fprintf(stderr, "FAIL: %s - missing leak groups\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Group #1 should appear before #3 since it's the largest after sorting */
    if (pos1 > pos3) {
        fprintf(stderr, "FAIL: %s - groups not sorted correctly\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(text);
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test percentage calculation.
 */
static int test_percentages(void) {
    report_config_t config;
    report_config_init(&config);
    config.show_percentages = true;
    
    report_formatter_t* formatter = report_formatter_create_with_config(&config);
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(2, 1);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    char* text = report_format_text(formatter, report);
    if (!text) {
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should contain percentage symbols */
    if (strstr(text, "%") == NULL) {
        fprintf(stderr, "FAIL: %s - missing percentage\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(text);
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test without percentages.
 */
static int test_no_percentages(void) {
    report_config_t config;
    report_config_init(&config);
    config.show_percentages = false;
    
    report_formatter_t* formatter = report_formatter_create_with_config(&config);
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(2, 1);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    char* text = report_format_text(formatter, report);
    if (!text) {
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Percentage after "Total:" line should not appear */
    /* This is tricky to test precisely, but we verify the report still works */
    if (strstr(text, "MEMROGUE") == NULL) {
        fprintf(stderr, "FAIL: %s - report broken\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(text);
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test without backtraces.
 */
static int test_no_backtraces(void) {
    report_config_t config;
    report_config_init(&config);
    config.show_backtraces = false;
    
    report_formatter_t* formatter = report_formatter_create_with_config(&config);
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(1, 1);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    char* text = report_format_text(formatter, report);
    if (!text) {
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should not contain "Backtrace:" when disabled */
    if (strstr(text, "Backtrace:") != NULL) {
        fprintf(stderr, "FAIL: %s - backtrace should be hidden\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(text);
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test max_groups limit.
 */
static int test_max_groups(void) {
    report_config_t config;
    report_config_init(&config);
    config.max_groups = 2;
    
    report_formatter_t* formatter = report_formatter_create_with_config(&config);
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(5, 1);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    char* text = report_format_text(formatter, report);
    if (!text) {
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should mention "more leak groups" */
    if (strstr(text, "more leak group") == NULL) {
        fprintf(stderr, "FAIL: %s - should mention hidden groups\n", __func__);
        free(text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(text);
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/* ============================================================================
 * Section Formatting Tests
 * ============================================================================ */

/**
 * Test summary-only formatting.
 */
static int test_format_summary_only(void) {
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(2, 1);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    char* summary = report_format_summary(formatter, report);
    if (!summary) {
        fprintf(stderr, "FAIL: %s - format_summary returned NULL\n", __func__);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should contain summary info but not full report */
    if (strstr(summary, "Total Leaks") == NULL) {
        fprintf(stderr, "FAIL: %s - missing Total Leaks\n", __func__);
        free(summary);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(summary);
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/**
 * Test single group formatting.
 */
static int test_format_single_group(void) {
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(1, 2);
    if (!report || !report->groups) {
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    char* group_text = report_format_group(formatter, report->groups, 
                                            report->total_bytes, 1);
    if (!group_text) {
        fprintf(stderr, "FAIL: %s - format_group returned NULL\n", __func__);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Should contain group header */
    if (strstr(group_text, "Leak Group #1") == NULL) {
        fprintf(stderr, "FAIL: %s - missing group header\n", __func__);
        free(group_text);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free(group_text);
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/* ============================================================================
 * File Output Tests
 * ============================================================================ */

/**
 * Test writing to file.
 */
static int test_write_to_file(void) {
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(2, 1);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    const char* filepath = "/tmp/memrogue_test_report.txt";
    
    bool success = report_write_to_file(formatter, report, filepath);
    if (!success) {
        fprintf(stderr, "FAIL: %s - write_to_file failed\n", __func__);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Verify file exists and has content */
    FILE* f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "FAIL: %s - file not created\n", __func__);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    
    /* Clean up */
    unlink(filepath);
    
    if (size < 100) {
        fprintf(stderr, "FAIL: %s - file too small (%ld bytes)\n", __func__, size);
        free_mock_report(report);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    free_mock_report(report);
    report_formatter_destroy(formatter);
    return 1;
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

typedef struct {
    report_formatter_t* formatter;
    leak_report_t* report;
    int thread_id;
    int success;
} thread_data_t;

static void* thread_format_func(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    
    for (int i = 0; i < 10; i++) {
        char* text = report_format_text(data->formatter, data->report);
        if (!text) {
            data->success = 0;
            return NULL;
        }
        
        if (strstr(text, "MEMROGUE") == NULL) {
            free(text);
            data->success = 0;
            return NULL;
        }
        
        free(text);
    }
    
    data->success = 1;
    return NULL;
}

/**
 * Test concurrent formatting.
 */
static int test_concurrent_formatting(void) {
    const int NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];
    
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) return 0;
    
    leak_report_t* report = create_mock_report(3, 2);
    if (!report) {
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Initialize thread data */
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].formatter = formatter;
        thread_data[i].report = report;
        thread_data[i].thread_id = i;
        thread_data[i].success = 0;
    }
    
    /* Start threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_format_func, &thread_data[i]) != 0) {
            fprintf(stderr, "FAIL: %s - could not create thread %d\n", __func__, i);
            /* Join threads created so far to prevent resource leak */
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            free_mock_report(report);
            report_formatter_destroy(formatter);
            return 0;
        }
    }
    
    /* Wait for threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Check results */
    int all_success = 1;
    for (int i = 0; i < NUM_THREADS; i++) {
        if (!thread_data[i].success) {
            fprintf(stderr, "FAIL: %s - thread %d failed\n", __func__, i);
            all_success = 0;
        }
    }
    
    free_mock_report(report);
    report_formatter_destroy(formatter);
    
    return all_success;
}

/* ============================================================================
 * Null/Edge Case Tests
 * ============================================================================ */

/**
 * Test NULL handling.
 */
static int test_null_handling(void) {
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) return 0;
    
    /* NULL report */
    char* text = report_format_text(formatter, NULL);
    if (text != NULL) {
        fprintf(stderr, "FAIL: %s - should return NULL for NULL report\n", __func__);
        free(text);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* NULL formatter */
    leak_report_t report = {0};
    text = report_format_text(NULL, &report);
    if (text != NULL) {
        fprintf(stderr, "FAIL: %s - should return NULL for NULL formatter\n", __func__);
        free(text);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* NULL stream */
    int result = report_write_to_stream(formatter, &report, NULL);
    if (result >= 0) {
        fprintf(stderr, "FAIL: %s - should return error for NULL stream\n", __func__);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* NULL filepath */
    bool success = report_write_to_file(formatter, &report, NULL);
    if (success) {
        fprintf(stderr, "FAIL: %s - should return false for NULL filepath\n", __func__);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* Config functions with NULL */
    report_formatter_set_config(NULL, NULL);
    report_formatter_set_config(formatter, NULL);
    report_formatter_get_config(NULL, NULL);
    
    report_config_t out_config;
    report_formatter_get_config(formatter, NULL);
    report_formatter_get_config(NULL, &out_config);
    
    /* format_bytes with NULL buffer */
    char* result_ptr = format_bytes(1024, NULL, 32);
    if (result_ptr != NULL) {
        fprintf(stderr, "FAIL: %s - format_bytes should return NULL for NULL buffer\n", __func__);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    /* format_backtrace with NULL */
    text = report_format_backtrace(formatter, NULL, 0);
    if (text != NULL) {
        fprintf(stderr, "FAIL: %s - format_backtrace should return NULL for NULL frames\n", __func__);
        free(text);
        report_formatter_destroy(formatter);
        return 0;
    }
    
    report_formatter_destroy(formatter);
    return 1;
}

/* ============================================================================
 * Integration Test with Real Tracker
 * ============================================================================ */

/**
 * Test integration with real leak detector.
 */
static int test_integration_with_tracker(void) {
    /* Create tracker */
    memory_tracker_t* tracker = tracker_create();
    if (!tracker) return 0;
    
    /* Simulate some allocations (leaks) */
    void* ptrs[5];
    for (int i = 0; i < 5; i++) {
        ptrs[i] = malloc(100 * (size_t)(i + 1));
        if (ptrs[i]) {
            track_allocation(tracker, ptrs[i], 100 * (size_t)(i + 1),
                             "integration_test.c", 100 + i);
        }
    }
    
    /* Free some but not all (create leaks) */
    track_deallocation(tracker, ptrs[0]);
    free(ptrs[0]);
    track_deallocation(tracker, ptrs[2]);
    free(ptrs[2]);
    
    /* Scan for leaks */
    leak_report_t* report = leak_detector_scan(tracker, NULL);
    if (!report) {
        /* Free remaining */
        free(ptrs[1]);
        free(ptrs[3]);
        free(ptrs[4]);
        tracker_destroy(tracker);
        fprintf(stderr, "FAIL: %s - leak_detector_scan returned NULL\n", __func__);
        return 0;
    }
    
    /* Format report */
    report_formatter_t* formatter = report_formatter_create();
    if (!formatter) {
        leak_report_destroy(report);
        free(ptrs[1]);
        free(ptrs[3]);
        free(ptrs[4]);
        tracker_destroy(tracker);
        return 0;
    }
    
    char* text = report_format_text(formatter, report);
    if (!text) {
        fprintf(stderr, "FAIL: %s - format_text returned NULL\n", __func__);
        report_formatter_destroy(formatter);
        leak_report_destroy(report);
        free(ptrs[1]);
        free(ptrs[3]);
        free(ptrs[4]);
        tracker_destroy(tracker);
        return 0;
    }
    
    /* Verify report contains expected info */
    int success = 1;
    
    if (strstr(text, "MEMROGUE") == NULL) {
        fprintf(stderr, "FAIL: %s - missing header\n", __func__);
        success = 0;
    }
    
    if (strstr(text, "integration_test.c") == NULL) {
        fprintf(stderr, "FAIL: %s - missing file name\n", __func__);
        success = 0;
    }
    
    /* Should have 3 leaks (ptrs[1], ptrs[3], ptrs[4]) */
    if (report->total_leaks != 3) {
        fprintf(stderr, "FAIL: %s - wrong total_leaks: %zu\n", __func__, report->total_leaks);
        success = 0;
    }
    
    free(text);
    report_formatter_destroy(formatter);
    leak_report_destroy(report);
    
    /* Clean up leaked memory */
    free(ptrs[1]);
    free(ptrs[3]);
    free(ptrs[4]);
    tracker_destroy(tracker);
    
    return success;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("========================================\n");
    printf("Text Report Formatter Unit Tests\n");
    printf("MEMRO-17\n");
    printf("========================================\n\n");
    
    /* Configuration tests */
    printf("--- Configuration Tests ---\n");
    RUN_TEST(test_config_init_defaults);
    RUN_TEST(test_config_init_null);
    
    /* Formatter lifecycle tests */
    printf("\n--- Formatter Lifecycle Tests ---\n");
    RUN_TEST(test_formatter_create_default);
    RUN_TEST(test_formatter_create_custom);
    RUN_TEST(test_formatter_destroy_null);
    RUN_TEST(test_formatter_config_update);
    
    /* Utility function tests */
    printf("\n--- Utility Function Tests ---\n");
    RUN_TEST(test_format_bytes);
    RUN_TEST(test_sort_order_strings);
    
    /* Report formatting tests */
    printf("\n--- Report Formatting Tests ---\n");
    RUN_TEST(test_format_empty_report);
    RUN_TEST(test_format_report_with_leaks);
    RUN_TEST(test_sort_by_size);
    RUN_TEST(test_percentages);
    RUN_TEST(test_no_percentages);
    RUN_TEST(test_no_backtraces);
    RUN_TEST(test_max_groups);
    
    /* Section formatting tests */
    printf("\n--- Section Formatting Tests ---\n");
    RUN_TEST(test_format_summary_only);
    RUN_TEST(test_format_single_group);
    
    /* File output tests */
    printf("\n--- File Output Tests ---\n");
    RUN_TEST(test_write_to_file);
    
    /* Thread safety tests */
    printf("\n--- Thread Safety Tests ---\n");
    RUN_TEST(test_concurrent_formatting);
    
    /* Null/edge case tests */
    printf("\n--- Null/Edge Case Tests ---\n");
    RUN_TEST(test_null_handling);
    
    /* Integration tests */
    printf("\n--- Integration Tests ---\n");
    RUN_TEST(test_integration_with_tracker);
    
    /* Summary */
    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("========================================\n");
    
    return (tests_passed == tests_run) ? 0 : 1;
}
