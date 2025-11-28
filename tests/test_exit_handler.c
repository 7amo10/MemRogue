/**
 * @file test_exit_handler.c
 * @brief Unit tests for exit handler functionality.
 *
 * Tests exit handler registration, configuration, callbacks, and leak detection.
 * Note: Some tests must be run in separate processes to avoid atexit side effects.
 *
 * MEMRO-13: Exit Hook Implementation Tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

#include "memrogue_exit_handler.h"
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
 * Config Initialization Tests
 * ============================================================================ */

/**
 * Test that exit_handler_config_init sets correct defaults.
 */
static int test_config_init_defaults(void) {
    exit_handler_config_t config;
    memset(&config, 0xFF, sizeof(config));  /* Fill with garbage */
    
    exit_handler_config_init(&config);
    
    TEST_ASSERT(config.enabled == true, "enabled should be true");
    TEST_ASSERT(config.print_report_on_exit == true, "print_report_on_exit should be true");
    TEST_ASSERT(config.abort_on_leaks == false, "abort_on_leaks should be false");
    TEST_ASSERT(config.leak_threshold == 0, "leak_threshold should be 0");
    TEST_ASSERT(config.report_file == NULL, "report_file should be NULL");
    
    return 1;
}

/**
 * Test that exit_handler_config_init handles NULL gracefully.
 */
static int test_config_init_null(void) {
    /* Should not crash */
    exit_handler_config_init(NULL);
    return 1;
}

/* ============================================================================
 * Registration Tests
 * ============================================================================ */

/**
 * Test basic registration with a tracker.
 */
static int test_register_basic(void) {
    memory_tracker_t* tracker = tracker_create();
    TEST_ASSERT(tracker != NULL, "tracker should be created");
    
    /* Should not be registered initially */
    TEST_ASSERT(!exit_handler_is_registered(), "should not be registered initially");
    
    /* Register with NULL config (defaults) */
    bool result = exit_handler_register(tracker, NULL);
    TEST_ASSERT(result == true, "registration should succeed");
    TEST_ASSERT(exit_handler_is_registered(), "should be registered after registration");
    
    /* Unregister */
    exit_handler_unregister();
    TEST_ASSERT(!exit_handler_is_registered(), "should not be registered after unregister");
    
    tracker_destroy(tracker);
    return 1;
}

/**
 * Test registration with custom configuration.
 */
static int test_register_with_config(void) {
    memory_tracker_t* tracker = tracker_create();
    TEST_ASSERT(tracker != NULL, "tracker should be created");
    
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.print_report_on_exit = false;
    config.abort_on_leaks = true;
    config.leak_threshold = 1024;
    
    bool result = exit_handler_register(tracker, &config);
    TEST_ASSERT(result == true, "registration with config should succeed");
    TEST_ASSERT(exit_handler_is_registered(), "should be registered");
    
    exit_handler_unregister();
    tracker_destroy(tracker);
    return 1;
}

/**
 * Test that re-registration updates tracker and config.
 */
static int test_reregister_updates(void) {
    int result = 0;
    memory_tracker_t* tracker1 = tracker_create();
    memory_tracker_t* tracker2 = tracker_create();
    if (!(tracker1 != NULL && tracker2 != NULL)) {
        fprintf(stderr, "FAIL: %s - trackers should be created (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    /* First registration */
    exit_handler_config_t config1;
    exit_handler_config_init(&config1);
    bool result1 = exit_handler_register(tracker1, &config1);
    if (!result1) {
        fprintf(stderr, "FAIL: %s - first registration should succeed (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    /* Second registration should succeed and update tracker */
    exit_handler_config_t config2;
    exit_handler_config_init(&config2);
    config2.print_report_on_exit = false;
    bool result2 = exit_handler_register(tracker2, &config2);
    if (!result2) {
        fprintf(stderr, "FAIL: %s - re-registration should succeed (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    if (!exit_handler_is_registered()) {
        fprintf(stderr, "FAIL: %s - should still be registered (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    result = 1;  /* Success */

cleanup:
    exit_handler_unregister();
    if (tracker1) tracker_destroy(tracker1);
    if (tracker2) tracker_destroy(tracker2);
    return result;
}

/* ============================================================================
 * Destructor Enable/Disable Tests
 * ============================================================================ */

/**
 * Test destructor enable/disable functionality.
 */
static int test_destructor_control(void) {
    /* Should be enabled by default */
    TEST_ASSERT(exit_handler_is_destructor_enabled() == true, 
                "destructor should be enabled by default");
    
    /* Disable it */
    exit_handler_set_destructor_enabled(false);
    TEST_ASSERT(exit_handler_is_destructor_enabled() == false,
                "destructor should be disabled after set_destructor_enabled(false)");
    
    /* Re-enable it */
    exit_handler_set_destructor_enabled(true);
    TEST_ASSERT(exit_handler_is_destructor_enabled() == true,
                "destructor should be re-enabled");
    
    return 1;
}

/* ============================================================================
 * Callback Tests
 * ============================================================================ */

static size_t callback_leaked_count = 0;
static size_t callback_leaked_bytes = 0;
static int callback_invoked = 0;

static void test_callback(memory_tracker_t* tracker, size_t leaked_count, 
                          size_t leaked_bytes, void* user_data) {
    (void)tracker;
    int* user_flag = (int*)user_data;
    callback_leaked_count = leaked_count;
    callback_leaked_bytes = leaked_bytes;
    callback_invoked = 1;
    if (user_flag) {
        *user_flag = 42;
    }
}

/**
 * Test that custom callback is invoked during exit_handler_run_now.
 */
static int test_callback_invocation(void) {
    int result = 0;
    memory_tracker_t* tracker = NULL;
    void* ptr = NULL;
    
    tracker = tracker_create();
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker should be created (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    /* Reset callback tracking */
    callback_invoked = 0;
    callback_leaked_count = 0;
    callback_leaked_bytes = 0;
    
    int user_flag = 0;
    exit_handler_set_callback(test_callback, &user_flag);
    
    /* Register and create a leak */
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.print_report_on_exit = false;  /* Suppress output for test */
    exit_handler_register(tracker, &config);
    
    /* Track an allocation (simulating a leak) */
    ptr = malloc(256);
    if (ptr == NULL) {
        fprintf(stderr, "FAIL: %s - malloc should succeed (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    track_allocation(tracker, ptr, 256, __FILE__, __LINE__);
    
    /* Run handler manually */
    size_t leaks = exit_handler_run_now();
    
    if (callback_invoked != 1) {
        fprintf(stderr, "FAIL: %s - callback should be invoked (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    if (callback_leaked_count != 1) {
        fprintf(stderr, "FAIL: %s - callback should receive leak count of 1 (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    if (callback_leaked_bytes != 256) {
        fprintf(stderr, "FAIL: %s - callback should receive leaked bytes of 256 (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    if (user_flag != 42) {
        fprintf(stderr, "FAIL: %s - user_data should be passed to callback (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    if (leaks != 1) {
        fprintf(stderr, "FAIL: %s - run_now should return 1 leak (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    result = 1;  /* Success */

cleanup:
    if (tracker && ptr) track_deallocation(tracker, ptr);
    if (ptr) free(ptr);
    exit_handler_set_callback(NULL, NULL);
    exit_handler_unregister();
    if (tracker) tracker_destroy(tracker);
    
    return result;
}

/**
 * Test removing callback with NULL.
 */
static int test_callback_removal(void) {
    memory_tracker_t* tracker = tracker_create();
    TEST_ASSERT(tracker != NULL, "tracker should be created");
    
    callback_invoked = 0;
    exit_handler_set_callback(test_callback, NULL);
    exit_handler_set_callback(NULL, NULL);  /* Remove callback */
    
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.print_report_on_exit = false;
    exit_handler_register(tracker, &config);
    
    exit_handler_run_now();
    
    TEST_ASSERT(callback_invoked == 0, "callback should not be invoked after removal");
    
    exit_handler_unregister();
    tracker_destroy(tracker);
    
    return 1;
}

/* ============================================================================
 * Leak Detection Tests (run_now)
 * ============================================================================ */

/**
 * Test that run_now returns 0 when no leaks.
 */
static int test_run_now_no_leaks(void) {
    memory_tracker_t* tracker = tracker_create();
    TEST_ASSERT(tracker != NULL, "tracker should be created");
    
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.print_report_on_exit = false;
    exit_handler_register(tracker, &config);
    
    /* Track and immediately free - no leaks */
    void* ptr = malloc(128);
    TEST_ASSERT(ptr != NULL, "malloc should succeed");
    track_allocation(tracker, ptr, 128, __FILE__, __LINE__);
    track_deallocation(tracker, ptr);
    free(ptr);
    
    size_t leaks = exit_handler_run_now();
    TEST_ASSERT(leaks == 0, "should report 0 leaks");
    
    exit_handler_unregister();
    tracker_destroy(tracker);
    
    return 1;
}

/**
 * Test that run_now detects multiple leaks.
 */
static int test_run_now_multiple_leaks(void) {
    int result = 0;
    memory_tracker_t* tracker = NULL;
    void* ptrs[5] = {NULL};
    int allocated_count = 0;
    
    tracker = tracker_create();
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker should be created (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.print_report_on_exit = false;
    exit_handler_register(tracker, &config);
    
    /* Create multiple leaks */
    for (int i = 0; i < 5; i++) {
        ptrs[i] = malloc(100 * (size_t)(i + 1));
        if (ptrs[i] == NULL) {
            fprintf(stderr, "FAIL: %s - malloc should succeed (line %d)\n", __func__, __LINE__);
            goto cleanup;
        }
        allocated_count++;
        track_allocation(tracker, ptrs[i], 100 * (size_t)(i + 1), __FILE__, __LINE__);
    }
    
    size_t leaks = exit_handler_run_now();
    if (leaks != 5) {
        fprintf(stderr, "FAIL: %s - should report 5 leaks (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    result = 1;  /* Success */

cleanup:
    /* Clean up all allocated memory */
    for (int i = 0; i < allocated_count; i++) {
        if (ptrs[i]) {
            track_deallocation(tracker, ptrs[i]);
            free(ptrs[i]);
        }
    }
    
    exit_handler_unregister();
    if (tracker) tracker_destroy(tracker);
    
    return result;
}

/* ============================================================================
 * Edge Case Tests
 * ============================================================================ */

/**
 * Test run_now before registration returns 0.
 */
static int test_run_now_before_register(void) {
    /* Make sure unregistered */
    exit_handler_unregister();
    
    size_t leaks = exit_handler_run_now();
    TEST_ASSERT(leaks == 0, "should return 0 before registration");
    
    return 1;
}

/**
 * Test run_now with NULL tracker returns 0.
 */
static int test_run_now_null_tracker(void) {
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.print_report_on_exit = false;
    
    /* Register with NULL tracker */
    exit_handler_register(NULL, &config);
    
    size_t leaks = exit_handler_run_now();
    TEST_ASSERT(leaks == 0, "should return 0 with NULL tracker");
    
    exit_handler_unregister();
    
    return 1;
}

/**
 * Test that disabled handler does not run.
 */
static int test_disabled_handler(void) {
    memory_tracker_t* tracker = tracker_create();
    TEST_ASSERT(tracker != NULL, "tracker should be created");
    
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.enabled = false;  /* Disable handler */
    config.print_report_on_exit = false;
    exit_handler_register(tracker, &config);
    
    /* Create a leak */
    void* ptr = malloc(64);
    TEST_ASSERT(ptr != NULL, "malloc should succeed");
    track_allocation(tracker, ptr, 64, __FILE__, __LINE__);
    
    /* Handler is disabled, should not be considered registered */
    TEST_ASSERT(!exit_handler_is_registered(), 
                "disabled handler should not be considered registered");
    
    /* Clean up */
    track_deallocation(tracker, ptr);
    free(ptr);
    exit_handler_unregister();
    tracker_destroy(tracker);
    
    return 1;
}

/* ============================================================================
 * Report File Tests
 * ============================================================================ */

/**
 * Test writing leak report to a file.
 */
static int test_report_to_file(void) {
    int result = 0;
    memory_tracker_t* tracker = NULL;
    void* ptr = NULL;
    FILE* f = NULL;
    int file_created = 0;
    char report_path[256];
    
    tracker = tracker_create();
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker should be created (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    /* Use TMPDIR environment variable for portability */
    const char* tmpdir = getenv("TMPDIR");
    if (!tmpdir || strlen(tmpdir) == 0) tmpdir = "/tmp";
    int written = snprintf(report_path, sizeof(report_path), "%s/memrogue_test_report_%d.txt", tmpdir, (int)getpid());
    if (written < 0 || (size_t)written >= sizeof(report_path)) {
        fprintf(stderr, "FAIL: %s - report path too long (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.print_report_on_exit = true;
    config.report_file = report_path;
    exit_handler_register(tracker, &config);
    
    /* Create a leak */
    ptr = malloc(512);
    if (ptr == NULL) {
        fprintf(stderr, "FAIL: %s - malloc should succeed (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    track_allocation(tracker, ptr, 512, "test_file.c", 42);
    
    /* Run handler - should write to file */
    exit_handler_run_now();
    file_created = 1;
    
    /* Verify file was created and has content */
    f = fopen(report_path, "r");
    if (f == NULL) {
        fprintf(stderr, "FAIL: %s - report file should exist (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    char buffer[256];
    char* line = fgets(buffer, sizeof(buffer), f);
    if (line == NULL) {
        fprintf(stderr, "FAIL: %s - report file should have content (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    if (strstr(buffer, "MemRogue") == NULL) {
        fprintf(stderr, "FAIL: %s - report should contain MemRogue prefix (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    result = 1;  /* Success */

cleanup:
    if (f) fclose(f);
    if (file_created) unlink(report_path);
    if (tracker && ptr) track_deallocation(tracker, ptr);
    if (ptr) free(ptr);
    exit_handler_unregister();
    if (tracker) tracker_destroy(tracker);
    
    return result;
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

static void* thread_register_func(void* arg) {
    memory_tracker_t* tracker = (memory_tracker_t*)arg;
    for (int i = 0; i < 100; i++) {
        exit_handler_config_t config;
        exit_handler_config_init(&config);
        config.print_report_on_exit = false;
        exit_handler_register(tracker, &config);
        exit_handler_is_registered();
        exit_handler_set_destructor_enabled(i % 2 == 0);
    }
    return NULL;
}

/**
 * Test concurrent registration operations.
 */
static int test_thread_safety(void) {
    int result = 0;
    memory_tracker_t* tracker = NULL;
    int threads_created = 0;
    pthread_t threads[4];
    
    tracker = tracker_create();
    if (tracker == NULL) {
        fprintf(stderr, "FAIL: %s - tracker should be created (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    for (int i = 0; i < 4; i++) {
        int rc = pthread_create(&threads[i], NULL, thread_register_func, tracker);
        if (rc != 0) {
            fprintf(stderr, "FAIL: %s - thread creation should succeed (line %d)\n", __func__, __LINE__);
            goto cleanup;
        }
        threads_created++;
    }
    
    for (int i = 0; i < threads_created; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Validate final state after concurrent operations */
    exit_handler_config_t config;
    exit_handler_config_init(&config);
    config.print_report_on_exit = false;
    exit_handler_register(tracker, &config);
    
    /* Verify run_now works correctly after concurrent access */
    size_t leaks = exit_handler_run_now();
    if (leaks != 0) {
        fprintf(stderr, "FAIL: %s - should report 0 leaks after thread safety test (line %d)\n", __func__, __LINE__);
        goto cleanup;
    }
    
    result = 1;  /* Success */

cleanup:
    exit_handler_unregister();
    if (tracker) tracker_destroy(tracker);
    
    return result;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("=== Exit Handler Tests ===\n\n");
    
    /* Config tests */
    RUN_TEST(test_config_init_defaults);
    RUN_TEST(test_config_init_null);
    
    /* Registration tests */
    RUN_TEST(test_register_basic);
    RUN_TEST(test_register_with_config);
    RUN_TEST(test_reregister_updates);
    
    /* Destructor control tests */
    RUN_TEST(test_destructor_control);
    
    /* Callback tests */
    RUN_TEST(test_callback_invocation);
    RUN_TEST(test_callback_removal);
    
    /* Leak detection tests */
    RUN_TEST(test_run_now_no_leaks);
    RUN_TEST(test_run_now_multiple_leaks);
    
    /* Edge case tests */
    RUN_TEST(test_run_now_before_register);
    RUN_TEST(test_run_now_null_tracker);
    RUN_TEST(test_disabled_handler);
    
    /* Report file tests */
    RUN_TEST(test_report_to_file);
    
    /* Thread safety tests */
    RUN_TEST(test_thread_safety);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return (tests_passed == tests_run) ? 0 : 1;
}
