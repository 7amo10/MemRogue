/**
 * @file test_invalid_free.c
 * @brief Unit tests for invalid free detection module.
 *
 * Tests cover:
 * - Configuration initialization
 * - Basic invalid free detection
 * - Violation reporting
 * - Distinguishing from double-free
 * - Configurable severity levels
 * - Thread safety
 * - Statistics tracking
 * - Callback functionality
 *
 * MEMRO-16: Invalid Free Detection
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>

#include "../include/memrogue_invalid_free.h"

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
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    
    if (config.enabled != true) {
        fprintf(stderr, "FAIL: %s - enabled not true\n", __func__);
        return 0;
    }
    
    if (config.severity != INVALID_FREE_SEVERITY_ERROR) {
        fprintf(stderr, "FAIL: %s - severity not ERROR\n", __func__);
        return 0;
    }
    
    if (config.print_on_error != true) {
        fprintf(stderr, "FAIL: %s - print_on_error not true\n", __func__);
        return 0;
    }
    
    if (config.initial_capacity != INVALID_FREE_DEFAULT_CAPACITY) {
        fprintf(stderr, "FAIL: %s - wrong initial_capacity\n", __func__);
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
    invalid_free_config_init(NULL);
    return 1;
}

/* ============================================================================
 * Detector Lifecycle Tests
 * ============================================================================ */

/**
 * Test creating detector with default config.
 */
static int test_detector_create_default(void) {
    invalid_free_detector_t* detector = invalid_free_detector_create();
    
    if (detector == NULL) {
        fprintf(stderr, "FAIL: %s - create returned NULL\n", __func__);
        return 0;
    }
    
    if (!invalid_free_is_enabled(detector)) {
        fprintf(stderr, "FAIL: %s - not enabled by default\n", __func__);
        invalid_free_detector_destroy(detector);
        return 0;
    }
    
    invalid_free_detector_destroy(detector);
    return 1;
}

/**
 * Test creating detector with custom config.
 */
static int test_detector_create_custom(void) {
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.initial_capacity = 100;
    config.severity = INVALID_FREE_SEVERITY_WARNING;
    config.print_on_error = false;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    
    if (detector == NULL) {
        fprintf(stderr, "FAIL: %s - create returned NULL\n", __func__);
        return 0;
    }
    
    invalid_free_detector_destroy(detector);
    return 1;
}

/**
 * Test destroying NULL detector.
 */
static int test_detector_destroy_null(void) {
    /* Should not crash */
    invalid_free_detector_destroy(NULL);
    return 1;
}

/* ============================================================================
 * Basic Detection Tests
 * ============================================================================ */

/**
 * Test that a valid free is accepted.
 */
static int test_valid_free_accepted(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;  /* Suppress output in tests */
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Record allocation */
    invalid_free_record_alloc(detector, ptr, 64, "test.c", 100);
    
    /* Free should be valid */
    if (!invalid_free_check_and_remove(detector, ptr, "test.c", 110)) {
        fprintf(stderr, "FAIL: %s - valid free marked as invalid\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Actually free the memory */
    free(ptr);
    
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/**
 * Test that invalid free (untracked pointer) is detected.
 */
static int test_invalid_free_detected(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;  /* Suppress output in tests */
    config.severity = INVALID_FREE_SEVERITY_ERROR;  /* Don't abort */
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    /* Create a fake pointer that was never tracked */
    void* fake_ptr = (void*)0xDEADBEEF;
    
    /* Free should be detected as invalid */
    if (invalid_free_check_and_remove(detector, fake_ptr, "test.c", 100)) {
        fprintf(stderr, "FAIL: %s - invalid free not detected\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/**
 * Test that freeing untracked malloc'd memory is detected.
 */
static int test_untracked_malloc_detected(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    config.severity = INVALID_FREE_SEVERITY_ERROR;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Don't record the allocation - just try to free it */
    /* This should detect invalid free since pointer is not tracked */
    if (invalid_free_check_and_remove(detector, ptr, "test.c", 100)) {
        fprintf(stderr, "FAIL: %s - untracked free not detected\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Actually free the memory */
    free(ptr);
    
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/**
 * Test that freeing same pointer twice (after removal) is detected.
 */
static int test_double_removal_detected(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    config.severity = INVALID_FREE_SEVERITY_ERROR;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Record allocation */
    invalid_free_record_alloc(detector, ptr, 64, "test.c", 100);
    
    /* First free should be valid */
    if (!invalid_free_check_and_remove(detector, ptr, "test.c", 110)) {
        fprintf(stderr, "FAIL: %s - first free failed\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Second free attempt should be detected as invalid */
    /* Note: The pointer is no longer tracked after first check_and_remove */
    if (invalid_free_check_and_remove(detector, ptr, "test.c", 120)) {
        fprintf(stderr, "FAIL: %s - second free not detected\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Actually free the memory */
    free(ptr);
    
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Violation Info Tests
 * ============================================================================ */

/**
 * Test that violation info is populated correctly.
 */
static int test_violation_info(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    config.severity = INVALID_FREE_SEVERITY_ERROR;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* fake_ptr = (void*)0xBAADF00D;
    
    /* Trigger invalid free */
    invalid_free_check_and_remove(detector, fake_ptr, "test_file.c", 42);
    
    /* Get violation info */
    invalid_free_violation_t violation;
    if (!invalid_free_get_last_violation(detector, &violation)) {
        fprintf(stderr, "FAIL: %s - no violation recorded\n", __func__);
        goto cleanup;
    }
    
    /* Verify violation contents */
    if (violation.address != fake_ptr) {
        fprintf(stderr, "FAIL: %s - wrong address\n", __func__);
        goto cleanup;
    }
    
    if (violation.type != INVALID_FREE_TYPE_UNTRACKED) {
        fprintf(stderr, "FAIL: %s - wrong type\n", __func__);
        goto cleanup;
    }
    
    if (violation.free_line != 42) {
        fprintf(stderr, "FAIL: %s - wrong line number\n", __func__);
        goto cleanup;
    }
    
    if (violation.free_file == NULL || strcmp(violation.free_file, "test_file.c") != 0) {
        fprintf(stderr, "FAIL: %s - wrong file name\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/**
 * Test violation formatting.
 */
static int test_violation_format(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    config.severity = INVALID_FREE_SEVERITY_ERROR;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* fake_ptr = (void*)0xCAFEBABE;
    
    /* Trigger invalid free */
    invalid_free_check_and_remove(detector, fake_ptr, "format_test.c", 99);
    
    /* Get violation info */
    invalid_free_violation_t violation;
    if (!invalid_free_get_last_violation(detector, &violation)) {
        fprintf(stderr, "FAIL: %s - no violation recorded\n", __func__);
        goto cleanup;
    }
    
    /* Format violation */
    char* formatted = invalid_free_format_violation(&violation);
    if (!formatted) {
        fprintf(stderr, "FAIL: %s - format returned NULL\n", __func__);
        goto cleanup;
    }
    
    /* Verify it contains key info */
    if (strstr(formatted, "INVALID FREE") == NULL) {
        fprintf(stderr, "FAIL: %s - missing header\n", __func__);
        free(formatted);
        goto cleanup;
    }
    
    if (strstr(formatted, "format_test.c") == NULL) {
        fprintf(stderr, "FAIL: %s - missing file name\n", __func__);
        free(formatted);
        goto cleanup;
    }
    
    free(formatted);
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Severity Level Tests
 * ============================================================================ */

/**
 * Test severity level configuration.
 */
static int test_severity_levels(void) {
    invalid_free_detector_t* detector = invalid_free_detector_create();
    if (!detector) return 0;
    
    /* Test setting and getting severity */
    invalid_free_set_severity(detector, INVALID_FREE_SEVERITY_WARNING);
    if (invalid_free_get_severity(detector) != INVALID_FREE_SEVERITY_WARNING) {
        fprintf(stderr, "FAIL: %s - WARNING level not set\n", __func__);
        invalid_free_detector_destroy(detector);
        return 0;
    }
    
    invalid_free_set_severity(detector, INVALID_FREE_SEVERITY_ERROR);
    if (invalid_free_get_severity(detector) != INVALID_FREE_SEVERITY_ERROR) {
        fprintf(stderr, "FAIL: %s - ERROR level not set\n", __func__);
        invalid_free_detector_destroy(detector);
        return 0;
    }
    
    invalid_free_set_severity(detector, INVALID_FREE_SEVERITY_FATAL);
    if (invalid_free_get_severity(detector) != INVALID_FREE_SEVERITY_FATAL) {
        fprintf(stderr, "FAIL: %s - FATAL level not set\n", __func__);
        invalid_free_detector_destroy(detector);
        return 0;
    }
    
    invalid_free_detector_destroy(detector);
    return 1;
}

/**
 * Test severity string conversions.
 */
static int test_severity_strings(void) {
    if (strcmp(invalid_free_severity_to_string(INVALID_FREE_SEVERITY_WARNING), "WARNING") != 0) {
        fprintf(stderr, "FAIL: %s - WARNING string\n", __func__);
        return 0;
    }
    
    if (strcmp(invalid_free_severity_to_string(INVALID_FREE_SEVERITY_ERROR), "ERROR") != 0) {
        fprintf(stderr, "FAIL: %s - ERROR string\n", __func__);
        return 0;
    }
    
    if (strcmp(invalid_free_severity_to_string(INVALID_FREE_SEVERITY_FATAL), "FATAL") != 0) {
        fprintf(stderr, "FAIL: %s - FATAL string\n", __func__);
        return 0;
    }
    
    return 1;
}

/**
 * Test type string conversions.
 */
static int test_type_strings(void) {
    if (strcmp(invalid_free_type_to_string(INVALID_FREE_TYPE_UNTRACKED), "UNTRACKED") != 0) {
        fprintf(stderr, "FAIL: %s - UNTRACKED string\n", __func__);
        return 0;
    }
    
    if (strcmp(invalid_free_type_to_string(INVALID_FREE_TYPE_ALREADY_FREED), "ALREADY_FREED") != 0) {
        fprintf(stderr, "FAIL: %s - ALREADY_FREED string\n", __func__);
        return 0;
    }
    
    return 1;
}

/* ============================================================================
 * Tracking Tests
 * ============================================================================ */

/**
 * Test is_tracked function.
 */
static int test_is_tracked(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Initially not tracked */
    if (invalid_free_is_tracked(detector, ptr)) {
        fprintf(stderr, "FAIL: %s - should not be tracked initially\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Record allocation */
    invalid_free_record_alloc(detector, ptr, 64, "test.c", 100);
    
    /* Now should be tracked */
    if (!invalid_free_is_tracked(detector, ptr)) {
        fprintf(stderr, "FAIL: %s - should be tracked after record\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Remove via check_and_remove */
    invalid_free_check_and_remove(detector, ptr, "test.c", 110);
    
    /* No longer tracked */
    if (invalid_free_is_tracked(detector, ptr)) {
        fprintf(stderr, "FAIL: %s - should not be tracked after removal\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    free(ptr);
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/**
 * Test realloc tracking.
 */
static int test_realloc_tracking(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr1 = malloc(64);
    if (!ptr1) goto cleanup;
    
    /* Record initial allocation */
    invalid_free_record_alloc(detector, ptr1, 64, "test.c", 100);
    
    /* Simulate realloc to new address */
    void* ptr2 = malloc(128);
    if (!ptr2) {
        free(ptr1);
        goto cleanup;
    }
    
    /* Record realloc */
    invalid_free_record_realloc(detector, ptr1, ptr2, 128, "test.c", 110);
    
    /* ptr1 should no longer be tracked */
    if (invalid_free_is_tracked(detector, ptr1)) {
        fprintf(stderr, "FAIL: %s - old pointer still tracked\n", __func__);
        free(ptr1);
        free(ptr2);
        goto cleanup;
    }
    
    /* ptr2 should be tracked */
    if (!invalid_free_is_tracked(detector, ptr2)) {
        fprintf(stderr, "FAIL: %s - new pointer not tracked\n", __func__);
        free(ptr1);
        free(ptr2);
        goto cleanup;
    }
    
    /* Free both pointers (ptr1 was never truly reallocated, just simulated) */
    free(ptr1);
    
    /* Valid free of ptr2 */
    if (!invalid_free_check_and_remove(detector, ptr2, "test.c", 120)) {
        fprintf(stderr, "FAIL: %s - valid free failed\n", __func__);
        free(ptr2);
        goto cleanup;
    }
    
    free(ptr2);
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
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
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    invalid_free_stats_t stats;
    
    /* Initial stats should be zero */
    invalid_free_get_stats(detector, &stats);
    if (stats.allocs_recorded != 0) {
        fprintf(stderr, "FAIL: %s - initial allocs not zero\n", __func__);
        goto cleanup;
    }
    
    /* Record some allocations */
    void* ptrs[5];
    for (int i = 0; i < 5; i++) {
        ptrs[i] = malloc(64);
        invalid_free_record_alloc(detector, ptrs[i], 64, "test.c", 100 + i);
    }
    
    invalid_free_get_stats(detector, &stats);
    if (stats.allocs_recorded != 5) {
        fprintf(stderr, "FAIL: %s - wrong allocs_recorded: %" PRIu64 "\n", 
                __func__, stats.allocs_recorded);
        for (int i = 0; i < 5; i++) free(ptrs[i]);
        goto cleanup;
    }
    
    if (stats.current_allocations != 5) {
        fprintf(stderr, "FAIL: %s - wrong current_allocations: %zu\n",
                __func__, stats.current_allocations);
        for (int i = 0; i < 5; i++) free(ptrs[i]);
        goto cleanup;
    }
    
    /* Free some */
    for (int i = 0; i < 3; i++) {
        invalid_free_check_and_remove(detector, ptrs[i], "test.c", 200 + i);
        free(ptrs[i]);
    }
    
    invalid_free_get_stats(detector, &stats);
    if (stats.frees_recorded != 3) {
        fprintf(stderr, "FAIL: %s - wrong frees_recorded: %" PRIu64 "\n",
                __func__, stats.frees_recorded);
        for (int i = 3; i < 5; i++) free(ptrs[i]);
        goto cleanup;
    }
    
    if (stats.current_allocations != 2) {
        fprintf(stderr, "FAIL: %s - wrong remaining allocations: %zu\n",
                __func__, stats.current_allocations);
        for (int i = 3; i < 5; i++) free(ptrs[i]);
        goto cleanup;
    }
    
    /* Peak should be 5 */
    if (stats.peak_allocations != 5) {
        fprintf(stderr, "FAIL: %s - wrong peak_allocations: %zu\n",
                __func__, stats.peak_allocations);
        for (int i = 3; i < 5; i++) free(ptrs[i]);
        goto cleanup;
    }
    
    /* Trigger invalid free */
    void* fake = (void*)0x12345678;
    invalid_free_check_and_remove(detector, fake, "test.c", 300);
    
    invalid_free_get_stats(detector, &stats);
    if (stats.invalid_frees_detected != 1) {
        fprintf(stderr, "FAIL: %s - wrong invalid_frees_detected: %" PRIu64 "\n",
                __func__, stats.invalid_frees_detected);
        for (int i = 3; i < 5; i++) free(ptrs[i]);
        goto cleanup;
    }
    
    /* Clean up remaining */
    for (int i = 3; i < 5; i++) {
        invalid_free_check_and_remove(detector, ptrs[i], "test.c", 400 + i);
        free(ptrs[i]);
    }
    
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/**
 * Test statistics reset.
 */
static int test_stats_reset(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    /* Record some allocations */
    void* ptr = malloc(64);
    invalid_free_record_alloc(detector, ptr, 64, "test.c", 100);
    
    /* Verify stats */
    invalid_free_stats_t stats;
    invalid_free_get_stats(detector, &stats);
    if (stats.allocs_recorded != 1) {
        fprintf(stderr, "FAIL: %s - stats not recorded\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Reset stats */
    invalid_free_reset_stats(detector);
    
    invalid_free_get_stats(detector, &stats);
    if (stats.allocs_recorded != 0) {
        fprintf(stderr, "FAIL: %s - allocs not reset\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* But allocation should still be tracked */
    if (!invalid_free_is_tracked(detector, ptr)) {
        fprintf(stderr, "FAIL: %s - allocation lost after reset\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    invalid_free_check_and_remove(detector, ptr, "test.c", 110);
    free(ptr);
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Callback Tests
 * ============================================================================ */

static int callback_count = 0;
static void* callback_last_address = NULL;

static void test_callback(const invalid_free_violation_t* violation, void* user_data) {
    callback_count++;
    callback_last_address = violation->address;
    
    int* counter = (int*)user_data;
    if (counter) {
        (*counter)++;
    }
}

/**
 * Test callback invocation.
 */
static int test_callback_invocation(void) {
    int result = 0;
    callback_count = 0;
    callback_last_address = NULL;
    int user_counter = 0;
    
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    config.severity = INVALID_FREE_SEVERITY_ERROR;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    /* Set callback */
    invalid_free_set_callback(detector, test_callback, &user_counter);
    
    /* Trigger invalid free */
    void* fake = (void*)0xABCDEF00;
    invalid_free_check_and_remove(detector, fake, "test.c", 100);
    
    /* Verify callback was called */
    if (callback_count != 1) {
        fprintf(stderr, "FAIL: %s - callback not called\n", __func__);
        goto cleanup;
    }
    
    if (callback_last_address != fake) {
        fprintf(stderr, "FAIL: %s - wrong address in callback\n", __func__);
        goto cleanup;
    }
    
    if (user_counter != 1) {
        fprintf(stderr, "FAIL: %s - user_data not passed\n", __func__);
        goto cleanup;
    }
    
    /* Remove callback */
    invalid_free_set_callback(detector, NULL, NULL);
    
    /* Trigger another invalid free */
    void* fake2 = (void*)0x12345600;
    invalid_free_check_and_remove(detector, fake2, "test.c", 200);
    
    /* Callback should not be called again */
    if (callback_count != 1) {
        fprintf(stderr, "FAIL: %s - callback called after removal\n", __func__);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Enable/Disable Tests
 * ============================================================================ */

/**
 * Test enable/disable functionality.
 */
static int test_enable_disable(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    void* ptr = malloc(64);
    if (!ptr) goto cleanup;
    
    /* Initially enabled */
    if (!invalid_free_is_enabled(detector)) {
        fprintf(stderr, "FAIL: %s - not enabled initially\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Record allocation */
    invalid_free_record_alloc(detector, ptr, 64, "test.c", 100);
    
    /* Disable detection */
    invalid_free_set_enabled(detector, false);
    
    if (invalid_free_is_enabled(detector)) {
        fprintf(stderr, "FAIL: %s - still enabled after disable\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Invalid free should return true when disabled */
    void* fake = (void*)0x99999999;
    if (!invalid_free_check_and_remove(detector, fake, "test.c", 200)) {
        fprintf(stderr, "FAIL: %s - should return true when disabled\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    /* Re-enable */
    invalid_free_set_enabled(detector, true);
    
    /* Valid free should still work */
    if (!invalid_free_check_and_remove(detector, ptr, "test.c", 300)) {
        fprintf(stderr, "FAIL: %s - valid free failed after re-enable\n", __func__);
        free(ptr);
        goto cleanup;
    }
    
    free(ptr);
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

typedef struct {
    invalid_free_detector_t* detector;
    int thread_id;
    int allocs;
    int frees;
    int invalid_frees;
} thread_data_t;

static void* thread_worker(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    void* ptrs[10];
    
    /* Allocate */
    for (int i = 0; i < 10; i++) {
        ptrs[i] = malloc(32);
        if (ptrs[i]) {
            invalid_free_record_alloc(data->detector, ptrs[i], 32, "thread.c", data->thread_id * 100 + i);
            data->allocs++;
        }
    }
    
    /* Free half */
    for (int i = 0; i < 5; i++) {
        if (ptrs[i]) {
            if (invalid_free_check_and_remove(data->detector, ptrs[i], "thread.c", data->thread_id * 100 + 50 + i)) {
                data->frees++;
            }
            free(ptrs[i]);
        }
    }
    
    /* Try invalid frees */
    for (int i = 0; i < 3; i++) {
        void* fake = (void*)(uintptr_t)((data->thread_id << 24) | (i << 8) | 0xFF);
        if (!invalid_free_check_and_remove(data->detector, fake, "thread.c", data->thread_id * 100 + 80 + i)) {
            data->invalid_frees++;
        }
    }
    
    /* Free rest */
    for (int i = 5; i < 10; i++) {
        if (ptrs[i]) {
            if (invalid_free_check_and_remove(data->detector, ptrs[i], "thread.c", data->thread_id * 100 + 90 + i)) {
                data->frees++;
            }
            free(ptrs[i]);
        }
    }
    
    return NULL;
}

/**
 * Test concurrent detection.
 */
static int test_concurrent_detection(void) {
    int result = 0;
    const int NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];
    
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    config.severity = INVALID_FREE_SEVERITY_ERROR;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    /* Initialize thread data */
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].detector = detector;
        thread_data[i].thread_id = i;
        thread_data[i].allocs = 0;
        thread_data[i].frees = 0;
        thread_data[i].invalid_frees = 0;
    }
    
    /* Start threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_worker, &thread_data[i]) != 0) {
            fprintf(stderr, "FAIL: %s - could not create thread %d\n", __func__, i);
            goto cleanup;
        }
    }
    
    /* Wait for threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Verify results */
    int total_allocs = 0;
    int total_frees = 0;
    int total_invalid = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        total_allocs += thread_data[i].allocs;
        total_frees += thread_data[i].frees;
        total_invalid += thread_data[i].invalid_frees;
    }
    
    /* Each thread allocates 10, frees all 10 */
    if (total_allocs != NUM_THREADS * 10) {
        fprintf(stderr, "FAIL: %s - wrong total allocs: %d\n", __func__, total_allocs);
        goto cleanup;
    }
    
    if (total_frees != NUM_THREADS * 10) {
        fprintf(stderr, "FAIL: %s - wrong total frees: %d\n", __func__, total_frees);
        goto cleanup;
    }
    
    /* Each thread does 3 invalid frees */
    if (total_invalid != NUM_THREADS * 3) {
        fprintf(stderr, "FAIL: %s - wrong total invalid: %d\n", __func__, total_invalid);
        goto cleanup;
    }
    
    /* Verify stats */
    invalid_free_stats_t stats;
    invalid_free_get_stats(detector, &stats);
    
    if (stats.invalid_frees_detected != (uint64_t)(NUM_THREADS * 3)) {
        fprintf(stderr, "FAIL: %s - stats invalid_frees wrong: %" PRIu64 "\n",
                __func__, stats.invalid_frees_detected);
        goto cleanup;
    }
    
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Address Reuse Tests
 * ============================================================================ */

/**
 * Test that address reuse is handled correctly.
 */
static int test_address_reuse(void) {
    int result = 0;
    invalid_free_config_t config;
    invalid_free_config_init(&config);
    config.print_on_error = false;
    
    invalid_free_detector_t* detector = invalid_free_detector_create_with_config(&config);
    if (!detector) goto cleanup;
    
    /* Allocate and free */
    void* ptr1 = malloc(64);
    if (!ptr1) goto cleanup;
    
    invalid_free_record_alloc(detector, ptr1, 64, "test.c", 100);
    invalid_free_check_and_remove(detector, ptr1, "test.c", 110);
    free(ptr1);
    
    /* Allocate again - might get same address */
    void* ptr2 = malloc(64);
    if (!ptr2) goto cleanup;
    
    /* Record new allocation */
    invalid_free_record_alloc(detector, ptr2, 64, "test.c", 200);
    
    /* Should be tracked */
    if (!invalid_free_is_tracked(detector, ptr2)) {
        fprintf(stderr, "FAIL: %s - reused address not tracked\n", __func__);
        free(ptr2);
        goto cleanup;
    }
    
    /* Valid free */
    if (!invalid_free_check_and_remove(detector, ptr2, "test.c", 210)) {
        fprintf(stderr, "FAIL: %s - valid free of ptr2 failed\n", __func__);
        free(ptr2);
        goto cleanup;
    }
    
    free(ptr2);
    result = 1;

cleanup:
    invalid_free_detector_destroy(detector);
    return result;
}

/* ============================================================================
 * Null/Edge Case Tests
 * ============================================================================ */

/**
 * Test NULL pointer handling.
 */
static int test_null_handling(void) {
    invalid_free_detector_t* detector = invalid_free_detector_create();
    if (!detector) return 0;
    
    /* These should not crash */
    invalid_free_record_alloc(detector, NULL, 64, "test.c", 100);
    invalid_free_check_and_remove(detector, NULL, "test.c", 100);
    invalid_free_is_tracked(detector, NULL);
    invalid_free_record_realloc(detector, NULL, NULL, 64, "test.c", 100);
    
    /* API calls with NULL detector */
    invalid_free_record_alloc(NULL, (void*)0x1000, 64, "test.c", 100);
    invalid_free_check_and_remove(NULL, (void*)0x1000, "test.c", 100);
    invalid_free_is_tracked(NULL, (void*)0x1000);
    invalid_free_set_enabled(NULL, true);
    invalid_free_is_enabled(NULL);
    invalid_free_set_severity(NULL, INVALID_FREE_SEVERITY_ERROR);
    invalid_free_get_severity(NULL);
    
    invalid_free_violation_t violation;
    invalid_free_get_last_violation(NULL, &violation);
    invalid_free_get_last_violation(detector, NULL);
    
    invalid_free_stats_t stats;
    invalid_free_get_stats(NULL, &stats);
    invalid_free_get_stats(detector, NULL);
    
    invalid_free_reset_stats(NULL);
    invalid_free_set_callback(NULL, NULL, NULL);
    
    /* Format NULL violation */
    if (invalid_free_format_violation(NULL) != NULL) {
        fprintf(stderr, "FAIL: %s - format NULL should return NULL\n", __func__);
        invalid_free_detector_destroy(detector);
        return 0;
    }
    
    /* Print NULL violation should not crash */
    invalid_free_print_violation(NULL);
    
    invalid_free_detector_destroy(detector);
    return 1;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("========================================\n");
    printf("Invalid Free Detection Unit Tests\n");
    printf("MEMRO-16\n");
    printf("========================================\n\n");
    
    /* Configuration tests */
    printf("--- Configuration Tests ---\n");
    RUN_TEST(test_config_init_defaults);
    RUN_TEST(test_config_init_null);
    
    /* Detector lifecycle tests */
    printf("\n--- Detector Lifecycle Tests ---\n");
    RUN_TEST(test_detector_create_default);
    RUN_TEST(test_detector_create_custom);
    RUN_TEST(test_detector_destroy_null);
    
    /* Basic detection tests */
    printf("\n--- Basic Detection Tests ---\n");
    RUN_TEST(test_valid_free_accepted);
    RUN_TEST(test_invalid_free_detected);
    RUN_TEST(test_untracked_malloc_detected);
    RUN_TEST(test_double_removal_detected);
    
    /* Violation info tests */
    printf("\n--- Violation Info Tests ---\n");
    RUN_TEST(test_violation_info);
    RUN_TEST(test_violation_format);
    
    /* Severity level tests */
    printf("\n--- Severity Level Tests ---\n");
    RUN_TEST(test_severity_levels);
    RUN_TEST(test_severity_strings);
    RUN_TEST(test_type_strings);
    
    /* Tracking tests */
    printf("\n--- Tracking Tests ---\n");
    RUN_TEST(test_is_tracked);
    RUN_TEST(test_realloc_tracking);
    
    /* Statistics tests */
    printf("\n--- Statistics Tests ---\n");
    RUN_TEST(test_statistics);
    RUN_TEST(test_stats_reset);
    
    /* Callback tests */
    printf("\n--- Callback Tests ---\n");
    RUN_TEST(test_callback_invocation);
    
    /* Enable/disable tests */
    printf("\n--- Enable/Disable Tests ---\n");
    RUN_TEST(test_enable_disable);
    
    /* Thread safety tests */
    printf("\n--- Thread Safety Tests ---\n");
    RUN_TEST(test_concurrent_detection);
    
    /* Address reuse tests */
    printf("\n--- Address Reuse Tests ---\n");
    RUN_TEST(test_address_reuse);
    
    /* Null/edge case tests */
    printf("\n--- Null/Edge Case Tests ---\n");
    RUN_TEST(test_null_handling);
    
    /* Summary */
    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("========================================\n");
    
    return (tests_passed == tests_run) ? 0 : 1;
}
