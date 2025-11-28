/**
 * @file test_double_free.c
 * @brief Unit tests for double-free detection module.
 *
 * Tests cover:
 * - Configuration initialization
 * - Basic double-free detection
 * - Violation reporting
 * - LRU cache eviction
 * - Thread safety
 * - Statistics tracking
 * - Callback functionality
 *
 * MEMRO-15: Double-Free Detection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

#include "../include/memrogue_double_free.h"

/*
 * Disable "use-after-free" warnings for this file.
 * These tests intentionally use pointers after free() to test
 * double-free detection functionality.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wuse-after-free"
#endif

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
 * Configuration Tests
 * ============================================================================ */

/**
 * Test default configuration initialization.
 */
static int test_config_init_defaults(void) {
    double_free_config_t config;
    double_free_config_init(&config);
    
    if (config.enabled != true) {
        fprintf(stderr, "FAIL: %s - enabled not true\n", __func__);
        return 0;
    }
    
    if (config.abort_on_error != false) {
        fprintf(stderr, "FAIL: %s - abort_on_error not false\n", __func__);
        return 0;
    }
    
    if (config.print_on_error != true) {
        fprintf(stderr, "FAIL: %s - print_on_error not true\n", __func__);
        return 0;
    }
    
    if (config.cache_size != DOUBLE_FREE_DEFAULT_CACHE_SIZE) {
        fprintf(stderr, "FAIL: %s - wrong cache_size\n", __func__);
        return 0;
    }
    
    if (config.backtrace_skip_frames != 2) {
        fprintf(stderr, "FAIL: %s - wrong backtrace_skip_frames\n", __func__);
        return 0;
    }
    
    return 1;
}

/**
 * Test NULL configuration handling.
 */
static int test_config_init_null(void) {
    /* Should not crash */
    double_free_config_init(NULL);
    return 1;
}

/* ============================================================================
 * Detector Lifecycle Tests
 * ============================================================================ */

/**
 * Test creating detector with default config.
 */
static int test_detector_create_default(void) {
    double_free_detector_t* detector = double_free_detector_create();
    
    if (detector == NULL) {
        fprintf(stderr, "FAIL: %s - create returned NULL\n", __func__);
        return 0;
    }
    
    if (!double_free_is_enabled(detector)) {
        fprintf(stderr, "FAIL: %s - not enabled by default\n", __func__);
        double_free_detector_destroy(detector);
        return 0;
    }
    
    double_free_detector_destroy(detector);
    return 1;
}

/**
 * Test creating detector with custom config.
 */
static int test_detector_create_custom(void) {
    double_free_config_t config;
    double_free_config_init(&config);
    config.cache_size = 100;
    config.abort_on_error = false;
    config.print_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    
    if (detector == NULL) {
        fprintf(stderr, "FAIL: %s - create returned NULL\n", __func__);
        return 0;
    }
    
    double_free_detector_destroy(detector);
    return 1;
}

/**
 * Test destroying NULL detector.
 */
static int test_detector_destroy_null(void) {
    /* Should not crash */
    double_free_detector_destroy(NULL);
    return 1;
}

/* ============================================================================
 * Basic Detection Tests
 * ============================================================================ */

/**
 * Test that a single free is valid.
 */
static int test_single_free_valid(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;  /* Suppress output in tests */
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Record allocation */
    double_free_record_alloc(detector, ptr, 64, "test.c", 100);
    
    /* First free should be valid */
    if (!double_free_check_and_record(detector, ptr, "test.c", 110)) {
        fprintf(stderr, "FAIL: %s - single free marked as double-free\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Actually free the memory */
    free(ptr);
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/**
 * Test that double-free is detected.
 */
static int test_double_free_detected(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;  /* Suppress output in tests */
    config.abort_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Record allocation */
    double_free_record_alloc(detector, ptr, 64, "test.c", 100);
    
    /* First free should be valid */
    if (!double_free_check_and_record(detector, ptr, "test.c", 110)) {
        fprintf(stderr, "FAIL: %s - first free failed\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Actually free the memory */
    free(ptr);
    
    /* Second "free" should be detected as double-free */
    /* Note: We're not actually calling free() again, just checking detection */
    if (double_free_check_and_record(detector, ptr, "test.c", 120)) {
        fprintf(stderr, "FAIL: %s - double-free not detected\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/**
 * Test that unrecorded allocations are handled gracefully.
 */
static int test_unrecorded_alloc_free(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Free without recording allocation - should still work */
    if (!double_free_check_and_record(detector, ptr, "test.c", 100)) {
        fprintf(stderr, "FAIL: %s - unrecorded free marked as double-free\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    free(ptr);
    
    /* Second free should still be detected */
    if (double_free_check_and_record(detector, ptr, "test.c", 110)) {
        fprintf(stderr, "FAIL: %s - double-free not detected\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Violation Information Tests
 * ============================================================================ */

/**
 * Test that violation contains correct information.
 */
static int test_violation_info(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    config.abort_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(128);
    if (!ptr) goto cleanup;
    
    /* Record allocation */
    double_free_record_alloc(detector, ptr, 128, "alloc.c", 50);
    
    /* First free */
    double_free_check_and_record(detector, ptr, "free1.c", 100);
    free(ptr);
    
    /* Second free (double-free) */
    double_free_check_and_record(detector, ptr, "free2.c", 200);
    
    /* Check violation */
    double_free_violation_t v;
    if (!double_free_get_last_violation(detector, &v)) {
        fprintf(stderr, "FAIL: %s - no violation recorded\n", __func__);
        goto cleanup;
    }
    
    if (v.address != ptr) {
        fprintf(stderr, "FAIL: %s - wrong address\n", __func__);
        goto cleanup;
    }
    
    if (v.size != 128) {
        fprintf(stderr, "FAIL: %s - wrong size\n", __func__);
        goto cleanup;
    }
    
    if (strcmp(v.alloc_file, "alloc.c") != 0 || v.alloc_line != 50) {
        fprintf(stderr, "FAIL: %s - wrong alloc info\n", __func__);
        goto cleanup;
    }
    
    if (strcmp(v.first_free_file, "free1.c") != 0 || v.first_free_line != 100) {
        fprintf(stderr, "FAIL: %s - wrong first free info\n", __func__);
        goto cleanup;
    }
    
    if (strcmp(v.second_free_file, "free2.c") != 0 || v.second_free_line != 200) {
        fprintf(stderr, "FAIL: %s - wrong second free info\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/**
 * Test violation formatting.
 */
static int test_violation_format(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    config.abort_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    double_free_record_alloc(detector, ptr, 64, "test.c", 10);
    double_free_check_and_record(detector, ptr, "test.c", 20);
    free(ptr);
    double_free_check_and_record(detector, ptr, "test.c", 30);
    
    double_free_violation_t v;
    if (!double_free_get_last_violation(detector, &v)) {
        fprintf(stderr, "FAIL: %s - no violation\n", __func__);
        goto cleanup;
    }
    
    char* formatted = double_free_format_violation(&v);
    if (!formatted) {
        fprintf(stderr, "FAIL: %s - format returned NULL\n", __func__);
        goto cleanup;
    }
    
    /* Check that key information is present */
    if (!strstr(formatted, "DOUBLE-FREE")) {
        fprintf(stderr, "FAIL: %s - missing header\n", __func__);
        free(formatted);
        goto cleanup;
    }
    
    if (!strstr(formatted, "First Free")) {
        fprintf(stderr, "FAIL: %s - missing first free\n", __func__);
        free(formatted);
        goto cleanup;
    }
    
    if (!strstr(formatted, "Second Free")) {
        fprintf(stderr, "FAIL: %s - missing second free\n", __func__);
        free(formatted);
        goto cleanup;
    }
    
    free(formatted);
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Cache Tests
 * ============================================================================ */

/**
 * Test LRU cache eviction.
 */
static int test_cache_eviction(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.cache_size = 10;  /* Small cache for testing */
    config.print_on_error = false;
    config.abort_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptrs[15];
    
    /* First allocate all pointers to prevent address reuse */
    for (int i = 0; i < 15; i++) {
        ptrs[i] = malloc(32);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) free(ptrs[j]);
            goto cleanup;
        }
        double_free_record_alloc(detector, ptrs[i], 32, "test.c", i);
    }
    
    /* Now free all pointers (more than cache size) */
    for (int i = 0; i < 15; i++) {
        double_free_check_and_record(detector, ptrs[i], "test.c", i + 100);
        free(ptrs[i]);
    }
    
    /* Check stats - should have evictions */
    double_free_stats_t stats;
    double_free_get_stats(detector, &stats);
    
    if (stats.cache_evictions == 0) {
        fprintf(stderr, "FAIL: %s - no evictions recorded (cache=%zu, evictions=%zu)\n", 
                __func__, stats.current_cache_entries, stats.cache_evictions);
        goto cleanup;
    }
    
    if (stats.current_cache_entries > 10) {
        fprintf(stderr, "FAIL: %s - cache exceeded size\n", __func__);
        goto cleanup;
    }
    
    /* First freed pointer should have been evicted, so double-free not detected */
    if (!double_free_check_and_record(detector, ptrs[0], "test.c", 500)) {
        fprintf(stderr, "FAIL: %s - evicted ptr detected as double-free\n", __func__);
        goto cleanup;
    }
    
    /* Last freed pointer should still be in cache */
    if (double_free_check_and_record(detector, ptrs[14], "test.c", 501)) {
        fprintf(stderr, "FAIL: %s - recent ptr not detected\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/**
 * Test address reuse clears cache entry.
 */
static int test_address_reuse(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* First allocation-free cycle */
    double_free_record_alloc(detector, ptr, 64, "test.c", 10);
    double_free_check_and_record(detector, ptr, "test.c", 20);
    free(ptr);
    
    /* Simulate address reuse - new allocation (may or may not be same address) */
    void* ptr2 = malloc(64);
    if (!ptr2) goto cleanup;
    
    /* Record new allocation at ptr2's address - should work normally */
    double_free_record_alloc(detector, ptr2, 64, "test.c", 30);
    
    /* Free of ptr2 should be valid (not double-free) */
    if (!double_free_check_and_record(detector, ptr2, "test.c", 40)) {
        fprintf(stderr, "FAIL: %s - new address marked as double-free\n", __func__);
        free(ptr2);
        goto cleanup;
    }
    
    free(ptr2);
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Callback Tests
 * ============================================================================ */

static int callback_count = 0;
static void* callback_last_addr = NULL;

static void test_callback(const double_free_violation_t* violation, void* user_data) {
    (void)user_data;
    callback_count++;
    callback_last_addr = violation->address;
}

/**
 * Test callback is invoked on double-free.
 */
static int test_callback_invoked(void) {
    int result = 0;
    callback_count = 0;
    callback_last_addr = NULL;
    
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    config.abort_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    double_free_set_callback(detector, test_callback, NULL);
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    double_free_record_alloc(detector, ptr, 64, "test.c", 10);
    double_free_check_and_record(detector, ptr, "test.c", 20);
    free(ptr);
    
    /* This should trigger callback */
    double_free_check_and_record(detector, ptr, "test.c", 30);
    
    if (callback_count != 1) {
        fprintf(stderr, "FAIL: %s - callback count %d != 1\n", __func__, callback_count);
        goto cleanup;
    }
    
    if (callback_last_addr != ptr) {
        fprintf(stderr, "FAIL: %s - wrong address in callback\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Statistics Tests
 * ============================================================================ */

/**
 * Test statistics tracking.
 */
static int test_statistics(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    config.abort_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr1 = malloc(64);
    void* ptr2 = malloc(64);
    if (!ptr1 || !ptr2) {
        free(ptr1);
        free(ptr2);
        goto cleanup;
    }
    
    /* Record allocations */
    double_free_record_alloc(detector, ptr1, 64, "test.c", 10);
    double_free_record_alloc(detector, ptr2, 64, "test.c", 20);
    
    /* Record frees */
    double_free_check_and_record(detector, ptr1, "test.c", 30);
    double_free_check_and_record(detector, ptr2, "test.c", 40);
    
    /* Trigger double-free */
    double_free_check_and_record(detector, ptr1, "test.c", 50);
    
    free(ptr1);
    free(ptr2);
    
    double_free_stats_t stats;
    double_free_get_stats(detector, &stats);
    
    if (stats.allocs_recorded != 2) {
        fprintf(stderr, "FAIL: %s - allocs_recorded %lu != 2\n",
                __func__, (unsigned long)stats.allocs_recorded);
        goto cleanup;
    }
    
    if (stats.frees_recorded != 2) {
        fprintf(stderr, "FAIL: %s - frees_recorded %lu != 2\n",
                __func__, (unsigned long)stats.frees_recorded);
        goto cleanup;
    }
    
    if (stats.double_frees_detected != 1) {
        fprintf(stderr, "FAIL: %s - double_frees_detected %lu != 1\n",
                __func__, (unsigned long)stats.double_frees_detected);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/**
 * Test statistics reset.
 */
static int test_statistics_reset(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    double_free_record_alloc(detector, ptr, 64, "test.c", 10);
    double_free_check_and_record(detector, ptr, "test.c", 20);
    free(ptr);
    
    /* Reset stats */
    double_free_reset_stats(detector);
    
    double_free_stats_t stats;
    double_free_get_stats(detector, &stats);
    
    if (stats.allocs_recorded != 0 || stats.frees_recorded != 0) {
        fprintf(stderr, "FAIL: %s - stats not reset\n", __func__);
        goto cleanup;
    }
    
    /* Cache entries should still be counted */
    if (stats.current_cache_entries != 1) {
        fprintf(stderr, "FAIL: %s - cache_entries wrong after reset\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Enable/Disable Tests
 * ============================================================================ */

/**
 * Test enabling/disabling detection.
 */
static int test_enable_disable(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    double_free_record_alloc(detector, ptr, 64, "test.c", 10);
    double_free_check_and_record(detector, ptr, "test.c", 20);
    free(ptr);
    
    /* Disable detection */
    double_free_set_enabled(detector, false);
    
    if (double_free_is_enabled(detector)) {
        fprintf(stderr, "FAIL: %s - still enabled after disable\n", __func__);
        goto cleanup;
    }
    
    /* Double-free should not be detected when disabled */
    if (!double_free_check_and_record(detector, ptr, "test.c", 30)) {
        fprintf(stderr, "FAIL: %s - detected when disabled\n", __func__);
        goto cleanup;
    }
    
    /* Re-enable */
    double_free_set_enabled(detector, true);
    
    if (!double_free_is_enabled(detector)) {
        fprintf(stderr, "FAIL: %s - not enabled after enable\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

typedef struct {
    double_free_detector_t* detector;
    int thread_id;
    int iterations;
    int detected_count;
} thread_context_t;

static void* thread_alloc_free_func(void* arg) {
    thread_context_t* ctx = (thread_context_t*)arg;
    
    for (int i = 0; i < ctx->iterations; i++) {
        void* ptr = malloc(32);
        if (!ptr) continue;
        
        double_free_record_alloc(ctx->detector, ptr, 32, "thread.c",
                                  ctx->thread_id * 1000 + i);
        
        /* Sometimes do double-free */
        if (i % 10 == 0) {
            double_free_check_and_record(ctx->detector, ptr, "thread.c",
                                          ctx->thread_id * 1000 + i + 500);
            free(ptr);
            
            /* Attempt double-free */
            if (!double_free_check_and_record(ctx->detector, ptr, "thread.c",
                                               ctx->thread_id * 1000 + i + 600)) {
                ctx->detected_count++;
            }
        } else {
            double_free_check_and_record(ctx->detector, ptr, "thread.c",
                                          ctx->thread_id * 1000 + i + 500);
            free(ptr);
        }
    }
    
    return NULL;
}

/**
 * Test concurrent access from multiple threads.
 */
static int test_concurrent_detection(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    config.abort_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    pthread_t threads[4];
    thread_context_t contexts[4];
    int threads_created = 0;
    
    /* Start threads */
    for (int i = 0; i < 4; i++) {
        contexts[i].detector = detector;
        contexts[i].thread_id = i;
        contexts[i].iterations = 100;
        contexts[i].detected_count = 0;
        
        if (pthread_create(&threads[i], NULL, thread_alloc_free_func,
                           &contexts[i]) != 0) {
            fprintf(stderr, "FAIL: %s - thread creation failed\n", __func__);
            goto wait_threads;
        }
        threads_created++;
    }
    
wait_threads:
    /* Wait for threads */
    for (int i = 0; i < threads_created; i++) {
        pthread_join(threads[i], NULL);
    }
    
    if (threads_created != 4) {
        goto cleanup;
    }
    
    /* Check that some double-frees were detected */
    int total_detected = 0;
    for (int i = 0; i < 4; i++) {
        total_detected += contexts[i].detected_count;
    }
    
    if (total_detected == 0) {
        fprintf(stderr, "FAIL: %s - no double-frees detected\n", __func__);
        goto cleanup;
    }
    
    /* Verify stats are consistent */
    double_free_stats_t stats;
    double_free_get_stats(detector, &stats);
    
    if (stats.double_frees_detected == 0) {
        fprintf(stderr, "FAIL: %s - stats show no double-frees\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Remove Record Tests
 * ============================================================================ */

/**
 * Test removing records (for realloc handling).
 */
static int test_remove_record(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Record allocation */
    double_free_record_alloc(detector, ptr, 64, "test.c", 10);
    
    /* Remove without freeing (simulating realloc) */
    double_free_remove_record(detector, ptr);
    
    /* Should be able to record again */
    double_free_record_alloc(detector, ptr, 128, "test.c", 20);
    
    /* Free should be valid */
    if (!double_free_check_and_record(detector, ptr, "test.c", 30)) {
        fprintf(stderr, "FAIL: %s - free after remove failed\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    free(ptr);
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * NULL Pointer Tests
 * ============================================================================ */

/**
 * Test NULL pointer handling.
 */
static int test_null_pointers(void) {
    int result = 0;
    double_free_config_t config;
    double_free_config_init(&config);
    config.print_on_error = false;
    
    double_free_detector_t* detector = double_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    /* These should not crash */
    double_free_record_alloc(detector, NULL, 64, "test.c", 10);
    double_free_check_and_record(detector, NULL, "test.c", 20);
    double_free_remove_record(detector, NULL);
    
    /* NULL detector should be handled */
    double_free_record_alloc(NULL, (void*)0x1000, 64, "test.c", 10);
    double_free_check_and_record(NULL, (void*)0x1000, "test.c", 20);
    double_free_remove_record(NULL, (void*)0x1000);
    
    result = 1;

cleanup:
    double_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("=== Double-Free Detector Tests ===\n\n");
    
    /* Configuration tests */
    RUN_TEST(test_config_init_defaults);
    RUN_TEST(test_config_init_null);
    
    /* Lifecycle tests */
    RUN_TEST(test_detector_create_default);
    RUN_TEST(test_detector_create_custom);
    RUN_TEST(test_detector_destroy_null);
    
    /* Basic detection tests */
    RUN_TEST(test_single_free_valid);
    RUN_TEST(test_double_free_detected);
    RUN_TEST(test_unrecorded_alloc_free);
    
    /* Violation tests */
    RUN_TEST(test_violation_info);
    RUN_TEST(test_violation_format);
    
    /* Cache tests */
    RUN_TEST(test_cache_eviction);
    RUN_TEST(test_address_reuse);
    
    /* Callback tests */
    RUN_TEST(test_callback_invoked);
    
    /* Statistics tests */
    RUN_TEST(test_statistics);
    RUN_TEST(test_statistics_reset);
    
    /* Enable/disable tests */
    RUN_TEST(test_enable_disable);
    
    /* Thread safety tests */
    RUN_TEST(test_concurrent_detection);
    
    /* Remove record tests */
    RUN_TEST(test_remove_record);
    
    /* NULL pointer tests */
    RUN_TEST(test_null_pointers);
    
    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    return (tests_passed == tests_run) ? 0 : 1;
}
