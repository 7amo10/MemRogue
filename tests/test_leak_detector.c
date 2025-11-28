/**
 * @file test_leak_detector.c
 * @brief Unit tests for leak detection engine.
 *
 * Tests leak detection, grouping by backtrace, and report generation.
 *
 * MEMRO-14: Leak Detection Engine Tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "memrogue_leak_detector.h"
#include "memrogue_tracker.h"

/* ============================================================================
 * Test Utilities
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s - %s (line %d)\n", __func__, msg, __LINE__); \
        return 0; \
    } \
} while(0)

#define RUN_TEST(test) do { \
    tests_run++; \
    printf("Running %s...\n", #test); \
    if (test()) { \
        tests_passed++; \
        printf("  PASSED\n"); \
    } else { \
        printf("  FAILED\n"); \
    } \
} while(0)

/* ============================================================================
 * Config Tests
 * ============================================================================ */

/**
 * Test that config init sets correct defaults.
 */
static int test_config_init_defaults(void) {
    leak_detector_config_t config;
    memset(&config, 0xFF, sizeof(config));
    
    leak_detector_config_init(&config);
    
    TEST_ASSERT(config.group_by_backtrace == true, "group_by_backtrace should be true");
    TEST_ASSERT(config.include_backtraces == true, "include_backtraces should be true");
    TEST_ASSERT(config.max_groups == 0, "max_groups should be 0 (unlimited)");
    TEST_ASSERT(config.max_entries_per_group == 10, "max_entries_per_group should be 10");
    TEST_ASSERT(config.min_leak_size == 0, "min_leak_size should be 0");
    
    return 1;
}

/**
 * Test that config init handles NULL gracefully.
 */
static int test_config_init_null(void) {
    leak_detector_config_init(NULL);  /* Should not crash */
    return 1;
}

/* ============================================================================
 * Empty Tracker Tests
 * ============================================================================ */

/**
 * Test scanning an empty tracker.
 */
static int test_scan_empty_tracker(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker creation failed\n", __func__);
        goto cleanup;
    }
    
    report = leak_detector_scan(tracker, NULL);
    if (report == NULL) {
        fprintf(stderr, "FAIL: %s - scan returned NULL\n", __func__);
        goto cleanup;
    }
    
    if (report->total_leaks != 0) {
        fprintf(stderr, "FAIL: %s - expected 0 leaks, got %zu\n", __func__, report->total_leaks);
        goto cleanup;
    }
    if (report->total_bytes != 0) {
        fprintf(stderr, "FAIL: %s - expected 0 bytes, got %zu\n", __func__, report->total_bytes);
        goto cleanup;
    }
    if (report->group_count != 0) {
        fprintf(stderr, "FAIL: %s - expected 0 groups, got %zu\n", __func__, report->group_count);
        goto cleanup;
    }
    if (report->severity != LEAK_SEVERITY_NONE) {
        fprintf(stderr, "FAIL: %s - expected NONE severity\n", __func__);
        goto cleanup;
    }
    if (leak_report_has_leaks(report)) {
        fprintf(stderr, "FAIL: %s - has_leaks should return false\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    if (report) leak_report_destroy(report);
    if (tracker) tracker_destroy(tracker);
    return result;
}

/**
 * Test scanning NULL tracker.
 */
static int test_scan_null_tracker(void) {
    leak_report_t* report = leak_detector_scan(NULL, NULL);
    TEST_ASSERT(report == NULL, "should return NULL for NULL tracker");
    return 1;
}

/* ============================================================================
 * Single Leak Tests
 * ============================================================================ */

/**
 * Test detecting a single leak.
 */
static int test_single_leak(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    void* ptr = NULL;
    
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker creation failed\n", __func__);
        goto cleanup;
    }
    
    ptr = malloc(256);
    if (ptr == NULL) {
        fprintf(stderr, "FAIL: %s - malloc failed\n", __func__);
        goto cleanup;
    }
    
    track_allocation(tracker, ptr, 256, "test.c", 42);
    
    report = leak_detector_scan(tracker, NULL);
    if (report == NULL) {
        fprintf(stderr, "FAIL: %s - scan returned NULL\n", __func__);
        goto cleanup;
    }
    
    if (report->total_leaks != 1) {
        fprintf(stderr, "FAIL: %s - expected 1 leak, got %zu\n", __func__, report->total_leaks);
        goto cleanup;
    }
    if (report->total_bytes != 256) {
        fprintf(stderr, "FAIL: %s - expected 256 bytes, got %zu\n", __func__, report->total_bytes);
        goto cleanup;
    }
    if (!leak_report_has_leaks(report)) {
        fprintf(stderr, "FAIL: %s - has_leaks should return true\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    if (report) leak_report_destroy(report);
    if (ptr) {
        track_deallocation(tracker, ptr);
        free(ptr);
    }
    if (tracker) tracker_destroy(tracker);
    return result;
}

/**
 * Test that freed allocations are not reported as leaks.
 */
static int test_no_leak_after_free(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker creation failed\n", __func__);
        goto cleanup;
    }
    
    void* ptr = malloc(128);
    if (ptr == NULL) {
        fprintf(stderr, "FAIL: %s - malloc failed\n", __func__);
        goto cleanup;
    }
    
    track_allocation(tracker, ptr, 128, "test.c", 10);
    track_deallocation(tracker, ptr);
    free(ptr);
    ptr = NULL;
    
    report = leak_detector_scan(tracker, NULL);
    if (report == NULL) {
        fprintf(stderr, "FAIL: %s - scan returned NULL\n", __func__);
        goto cleanup;
    }
    
    if (report->total_leaks != 0) {
        fprintf(stderr, "FAIL: %s - expected 0 leaks after free\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    if (report) leak_report_destroy(report);
    if (tracker) tracker_destroy(tracker);
    return result;
}

/* ============================================================================
 * Multiple Leak Tests
 * ============================================================================ */

/**
 * Test detecting multiple leaks.
 */
static int test_multiple_leaks(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    void* ptrs[5] = {NULL};
    int allocated = 0;
    
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker creation failed\n", __func__);
        goto cleanup;
    }
    
    size_t total_expected = 0;
    for (int i = 0; i < 5; i++) {
        size_t size = (size_t)(100 * (i + 1));
        ptrs[i] = malloc(size);
        if (ptrs[i] == NULL) {
            fprintf(stderr, "FAIL: %s - malloc failed\n", __func__);
            goto cleanup;
        }
        allocated++;
        track_allocation(tracker, ptrs[i], size, "test.c", 100 + i);
        total_expected += size;
    }
    
    report = leak_detector_scan(tracker, NULL);
    if (report == NULL) {
        fprintf(stderr, "FAIL: %s - scan returned NULL\n", __func__);
        goto cleanup;
    }
    
    if (report->total_leaks != 5) {
        fprintf(stderr, "FAIL: %s - expected 5 leaks, got %zu\n", __func__, report->total_leaks);
        goto cleanup;
    }
    if (report->total_bytes != total_expected) {
        fprintf(stderr, "FAIL: %s - expected %zu bytes, got %zu\n", 
                __func__, total_expected, report->total_bytes);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    if (report) leak_report_destroy(report);
    for (int i = 0; i < allocated; i++) {
        if (ptrs[i]) {
            track_deallocation(tracker, ptrs[i]);
            free(ptrs[i]);
        }
    }
    if (tracker) tracker_destroy(tracker);
    return result;
}

/* ============================================================================
 * Severity Tests
 * ============================================================================ */

/**
 * Test severity levels.
 */
static int test_severity_levels(void) {
    TEST_ASSERT(strcmp(leak_severity_to_string(LEAK_SEVERITY_NONE), "None") == 0,
                "NONE severity string");
    TEST_ASSERT(strcmp(leak_severity_to_string(LEAK_SEVERITY_LOW), "Low") == 0,
                "LOW severity string");
    TEST_ASSERT(strcmp(leak_severity_to_string(LEAK_SEVERITY_MEDIUM), "Medium") == 0,
                "MEDIUM severity string");
    TEST_ASSERT(strcmp(leak_severity_to_string(LEAK_SEVERITY_HIGH), "High") == 0,
                "HIGH severity string");
    TEST_ASSERT(strcmp(leak_severity_to_string(LEAK_SEVERITY_CRITICAL), "Critical") == 0,
                "CRITICAL severity string");
    
    return 1;
}

/**
 * Test that severity is computed correctly based on bytes.
 */
static int test_severity_computation(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    void* ptr = NULL;
    
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker creation failed\n", __func__);
        goto cleanup;
    }
    
    /* Test LOW severity (< 1KB) */
    ptr = malloc(512);
    if (ptr == NULL) goto cleanup;
    track_allocation(tracker, ptr, 512, "test.c", 1);
    
    report = leak_detector_scan(tracker, NULL);
    if (report == NULL) goto cleanup;
    
    if (report->severity != LEAK_SEVERITY_LOW) {
        fprintf(stderr, "FAIL: %s - expected LOW severity for 512 bytes\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    if (report) leak_report_destroy(report);
    if (ptr) {
        track_deallocation(tracker, ptr);
        free(ptr);
    }
    if (tracker) tracker_destroy(tracker);
    return result;
}

/* ============================================================================
 * Backtrace Signature Tests
 * ============================================================================ */

/**
 * Test backtrace signature computation.
 */
static int test_backtrace_signature(void) {
    void* frames1[] = {(void*)0x1000, (void*)0x2000, (void*)0x3000};
    void* frames2[] = {(void*)0x1000, (void*)0x2000, (void*)0x3000};
    void* frames3[] = {(void*)0x1000, (void*)0x2000, (void*)0x4000};
    
    uint64_t sig1 = backtrace_compute_signature(frames1, 3);
    uint64_t sig2 = backtrace_compute_signature(frames2, 3);
    uint64_t sig3 = backtrace_compute_signature(frames3, 3);
    
    TEST_ASSERT(sig1 != 0, "signature should not be 0");
    TEST_ASSERT(sig1 == sig2, "identical backtraces should have same signature");
    TEST_ASSERT(sig1 != sig3, "different backtraces should have different signatures");
    
    /* Test empty backtrace */
    uint64_t sig_empty = backtrace_compute_signature(NULL, 0);
    TEST_ASSERT(sig_empty == 0, "empty backtrace should have signature 0");
    
    return 1;
}

/**
 * Test backtrace equality comparison.
 */
static int test_backtrace_equals(void) {
    void* frames1[] = {(void*)0x1000, (void*)0x2000};
    void* frames2[] = {(void*)0x1000, (void*)0x2000};
    void* frames3[] = {(void*)0x1000, (void*)0x3000};
    void* frames4[] = {(void*)0x1000};
    
    TEST_ASSERT(backtrace_equals(frames1, 2, frames2, 2), "identical backtraces should be equal");
    TEST_ASSERT(!backtrace_equals(frames1, 2, frames3, 2), "different frames should not be equal");
    TEST_ASSERT(!backtrace_equals(frames1, 2, frames4, 1), "different lengths should not be equal");
    TEST_ASSERT(backtrace_equals(NULL, 0, NULL, 0), "both empty should be equal");
    
    return 1;
}

/* ============================================================================
 * Report Query Tests
 * ============================================================================ */

/**
 * Test largest group query.
 */
static int test_largest_group(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    void* ptr1 = NULL;
    void* ptr2 = NULL;
    
    if (tracker == NULL) goto cleanup;
    
    /* Disable backtrace grouping to get separate groups */
    leak_detector_config_t config;
    leak_detector_config_init(&config);
    config.group_by_backtrace = false;
    
    ptr1 = malloc(100);
    ptr2 = malloc(500);
    if (!ptr1 || !ptr2) goto cleanup;
    
    track_allocation(tracker, ptr1, 100, "test.c", 1);
    track_allocation(tracker, ptr2, 500, "test.c", 2);
    
    report = leak_detector_scan(tracker, &config);
    if (report == NULL) goto cleanup;
    
    const leak_group_t* largest = leak_report_largest_group(report);
    if (largest == NULL) {
        fprintf(stderr, "FAIL: %s - largest group should not be NULL\n", __func__);
        goto cleanup;
    }
    if (largest->total_bytes != 500) {
        fprintf(stderr, "FAIL: %s - largest group should have 500 bytes\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    if (report) leak_report_destroy(report);
    if (ptr1) { track_deallocation(tracker, ptr1); free(ptr1); }
    if (ptr2) { track_deallocation(tracker, ptr2); free(ptr2); }
    if (tracker) tracker_destroy(tracker);
    return result;
}

/**
 * Test most frequent group query.
 */
static int test_most_frequent_group(void) {
    /* Test on NULL report */
    TEST_ASSERT(leak_report_most_frequent_group(NULL) == NULL,
                "should return NULL for NULL report");
    return 1;
}

/**
 * Test has_leaks on NULL report.
 */
static int test_has_leaks_null(void) {
    TEST_ASSERT(!leak_report_has_leaks(NULL), "should return false for NULL report");
    return 1;
}

/* ============================================================================
 * Configuration Tests
 * ============================================================================ */

/**
 * Test minimum leak size filter.
 */
static int test_min_leak_size_filter(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    void* small_ptr = NULL;
    void* large_ptr = NULL;
    
    if (tracker == NULL) goto cleanup;
    
    small_ptr = malloc(50);
    large_ptr = malloc(200);
    if (!small_ptr || !large_ptr) goto cleanup;
    
    track_allocation(tracker, small_ptr, 50, "test.c", 1);
    track_allocation(tracker, large_ptr, 200, "test.c", 2);
    
    /* Set minimum leak size to 100 */
    leak_detector_config_t config;
    leak_detector_config_init(&config);
    config.min_leak_size = 100;
    
    report = leak_detector_scan(tracker, &config);
    if (report == NULL) goto cleanup;
    
    /* Should only report the large leak */
    if (report->total_leaks != 1) {
        fprintf(stderr, "FAIL: %s - expected 1 leak with min_size filter, got %zu\n",
                __func__, report->total_leaks);
        goto cleanup;
    }
    if (report->total_bytes != 200) {
        fprintf(stderr, "FAIL: %s - expected 200 bytes, got %zu\n",
                __func__, report->total_bytes);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    if (report) leak_report_destroy(report);
    if (small_ptr) { track_deallocation(tracker, small_ptr); free(small_ptr); }
    if (large_ptr) { track_deallocation(tracker, large_ptr); free(large_ptr); }
    if (tracker) tracker_destroy(tracker);
    return result;
}

/**
 * Test max entries per group limit.
 */
static int test_max_entries_limit(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    void* ptrs[20] = {NULL};
    int allocated = 0;
    
    if (tracker == NULL) goto cleanup;
    
    /* Create 20 leaks */
    for (int i = 0; i < 20; i++) {
        ptrs[i] = malloc(64);
        if (!ptrs[i]) goto cleanup;
        allocated++;
        track_allocation(tracker, ptrs[i], 64, "test.c", i);
    }
    
    /* Limit to 5 entries per group */
    leak_detector_config_t config;
    leak_detector_config_init(&config);
    config.max_entries_per_group = 5;
    config.group_by_backtrace = false;  /* Each leak in own group */
    
    report = leak_detector_scan(tracker, &config);
    if (report == NULL) goto cleanup;
    
    /* Should still report all 20 leaks in statistics */
    if (report->total_leaks != 20) {
        fprintf(stderr, "FAIL: %s - expected 20 leaks counted, got %zu\n",
                __func__, report->total_leaks);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    if (report) leak_report_destroy(report);
    for (int i = 0; i < allocated; i++) {
        if (ptrs[i]) {
            track_deallocation(tracker, ptrs[i]);
            free(ptrs[i]);
        }
    }
    if (tracker) tracker_destroy(tracker);
    return result;
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

typedef struct {
    memory_tracker_t* tracker;
    int thread_id;
    int allocations;
} thread_context_t;

static void* thread_allocate_func(void* arg) {
    thread_context_t* ctx = (thread_context_t*)arg;
    
    for (int i = 0; i < ctx->allocations; i++) {
        void* ptr = malloc(64);
        if (ptr) {
            track_allocation(ctx->tracker, ptr, 64, "thread.c", ctx->thread_id * 1000 + i);
            /* Intentionally don't free - creating leaks */
        }
    }
    
    return NULL;
}

/**
 * Test leak detection with concurrent allocations.
 */
static int test_concurrent_leak_detection(void) {
    int result = 0;
    memory_tracker_t* tracker = tracker_create();
    leak_report_t* report = NULL;
    pthread_t threads[4];
    thread_context_t contexts[4];
    int threads_created = 0;
    
    if (tracker == NULL) goto cleanup;
    
    /* Start threads that create leaks */
    for (int i = 0; i < 4; i++) {
        contexts[i].tracker = tracker;
        contexts[i].thread_id = i;
        contexts[i].allocations = 10;
        
        if (pthread_create(&threads[i], NULL, thread_allocate_func, &contexts[i]) != 0) {
            fprintf(stderr, "FAIL: %s - thread creation failed\n", __func__);
            goto cleanup;
        }
        threads_created++;
    }
    
    /* Wait for threads */
    for (int i = 0; i < threads_created; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Scan for leaks */
    report = leak_detector_scan(tracker, NULL);
    if (report == NULL) {
        fprintf(stderr, "FAIL: %s - scan returned NULL\n", __func__);
        goto cleanup;
    }
    
    /* Should have 40 leaks (4 threads * 10 allocations) */
    if (report->total_leaks != 40) {
        fprintf(stderr, "FAIL: %s - expected 40 leaks, got %zu\n",
                __func__, report->total_leaks);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    /* Free all intentionally leaked allocations before destroying the report */
    if (report) {
        for (leak_group_t* group = report->groups; group != NULL; group = group->next) {
            for (leak_entry_t* entry = group->entries; entry != NULL; entry = entry->next) {
                if (entry->address) {
                    free(entry->address);
                }
            }
        }
        leak_report_destroy(report);
    }
    if (tracker) tracker_destroy(tracker);
    return result;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("=== Leak Detector Tests ===\n\n");
    
    /* Config tests */
    RUN_TEST(test_config_init_defaults);
    RUN_TEST(test_config_init_null);
    
    /* Empty tracker tests */
    RUN_TEST(test_scan_empty_tracker);
    RUN_TEST(test_scan_null_tracker);
    
    /* Single leak tests */
    RUN_TEST(test_single_leak);
    RUN_TEST(test_no_leak_after_free);
    
    /* Multiple leak tests */
    RUN_TEST(test_multiple_leaks);
    
    /* Severity tests */
    RUN_TEST(test_severity_levels);
    RUN_TEST(test_severity_computation);
    
    /* Backtrace signature tests */
    RUN_TEST(test_backtrace_signature);
    RUN_TEST(test_backtrace_equals);
    
    /* Report query tests */
    RUN_TEST(test_largest_group);
    RUN_TEST(test_most_frequent_group);
    RUN_TEST(test_has_leaks_null);
    
    /* Configuration tests */
    RUN_TEST(test_min_leak_size_filter);
    RUN_TEST(test_max_entries_limit);
    
    /* Thread safety tests */
    RUN_TEST(test_concurrent_leak_detection);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return (tests_passed == tests_run) ? 0 : 1;
}
