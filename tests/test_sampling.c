/**
 * @file test_sampling.c
 * @brief Unit tests for MEMRO-21: Sampling Mode
 *
 * Tests the sampling mode functionality including:
 * - Random vs deterministic sampling
 * - Statistics extrapolation
 * - Thread safety of sampling counter
 * - Environment variable parsing
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>

#include "memrogue_config.h"
#include "memrogue_tracker.h"

/* ============================================================================
 * Test Framework
 * ============================================================================ */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "  FAILED: %s\n", message); \
        return false; \
    } \
} while(0)

#define TEST_ASSERT_EQ(actual, expected, message) do { \
    if ((actual) != (expected)) { \
        fprintf(stderr, "  FAILED: %s (expected %d, got %d)\n", \
                message, (int)(expected), (int)(actual)); \
        return false; \
    } \
} while(0)

#define TEST_ASSERT_RANGE(value, min, max, message) do { \
    if ((value) < (min) || (value) > (max)) { \
        fprintf(stderr, "  FAILED: %s (expected %d-%d, got %d)\n", \
                message, (int)(min), (int)(max), (int)(value)); \
        return false; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    g_tests_run++; \
    /* Reset config state before each test */ \
    unsetenv("MEMROGUE_SAMPLING_MODE"); \
    unsetenv("MEMROGUE_SAMPLE_RATE"); \
    config_reload(); \
    config_reset_sampling_counter(); \
    fprintf(stderr, "Running %s...\n", #test_func); \
    if (test_func()) { \
        g_tests_passed++; \
        fprintf(stderr, "  PASSED\n"); \
    } else { \
        g_tests_failed++; \
    } \
} while(0)

/* ============================================================================
 * Sampling Mode Enum Tests
 * ============================================================================ */

static bool test_sampling_mode_default(void) {
    /* Default should be random mode */
    memrogue_config_t config;
    config_init_defaults(&config);
    
    TEST_ASSERT_EQ(config.sampling_mode, MEMROGUE_SAMPLING_RANDOM,
                   "Default sampling mode should be random");
    
    return true;
}

static bool test_sampling_mode_env_random(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "random", 1);
    const memrogue_config_t* cfg = config_reload();
    
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_RANDOM,
                   "MEMROGUE_SAMPLING_MODE=random should set random mode");
    
    /* Test aliases */
    setenv("MEMROGUE_SAMPLING_MODE", "rand", 1);
    cfg = config_reload();
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_RANDOM,
                   "MEMROGUE_SAMPLING_MODE=rand should set random mode");
    
    setenv("MEMROGUE_SAMPLING_MODE", "r", 1);
    cfg = config_reload();
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_RANDOM,
                   "MEMROGUE_SAMPLING_MODE=r should set random mode");
    
    /* Test case insensitivity */
    setenv("MEMROGUE_SAMPLING_MODE", "RANDOM", 1);
    cfg = config_reload();
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_RANDOM,
                   "MEMROGUE_SAMPLING_MODE=RANDOM should set random mode");
    
    return true;
}

static bool test_sampling_mode_env_deterministic(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "deterministic", 1);
    const memrogue_config_t* cfg = config_reload();
    
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_DETERMINISTIC,
                   "MEMROGUE_SAMPLING_MODE=deterministic should set deterministic mode");
    
    /* Test aliases */
    setenv("MEMROGUE_SAMPLING_MODE", "det", 1);
    cfg = config_reload();
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_DETERMINISTIC,
                   "MEMROGUE_SAMPLING_MODE=det should set deterministic mode");
    
    setenv("MEMROGUE_SAMPLING_MODE", "d", 1);
    cfg = config_reload();
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_DETERMINISTIC,
                   "MEMROGUE_SAMPLING_MODE=d should set deterministic mode");
    
    setenv("MEMROGUE_SAMPLING_MODE", "nth", 1);
    cfg = config_reload();
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_DETERMINISTIC,
                   "MEMROGUE_SAMPLING_MODE=nth should set deterministic mode");
    
    return true;
}

static bool test_sampling_mode_env_invalid(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "invalid_mode", 1);
    const memrogue_config_t* cfg = config_reload();
    
    /* Invalid value should default to random */
    TEST_ASSERT_EQ(cfg->sampling_mode, MEMROGUE_SAMPLING_RANDOM,
                   "Invalid MEMROGUE_SAMPLING_MODE should default to random");
    
    return true;
}

/* ============================================================================
 * Deterministic Sampling Tests
 * ============================================================================ */

static bool test_deterministic_sampling_10_percent(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "deterministic", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "10", 1);
    config_reload();
    config_reset_sampling_counter();
    
    /* With 10% sample rate in deterministic mode, every 10th allocation should be sampled */
    int sampled_count = 0;
    for (int i = 0; i < 100; i++) {
        if (config_should_sample()) {
            sampled_count++;
        }
    }
    
    /* Should have exactly 10 samples (every 10th) */
    TEST_ASSERT_EQ(sampled_count, 10,
                   "Deterministic 10% sampling should sample exactly 10 out of 100");
    
    return true;
}

static bool test_deterministic_sampling_1_percent(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "deterministic", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "1", 1);
    config_reload();
    config_reset_sampling_counter();
    
    /* With 1% sample rate, every 100th allocation should be sampled */
    int sampled_count = 0;
    for (int i = 0; i < 1000; i++) {
        if (config_should_sample()) {
            sampled_count++;
        }
    }
    
    /* Should have exactly 10 samples (every 100th out of 1000) */
    TEST_ASSERT_EQ(sampled_count, 10,
                   "Deterministic 1% sampling should sample exactly 10 out of 1000");
    
    return true;
}

static bool test_deterministic_sampling_50_percent(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "deterministic", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "50", 1);
    config_reload();
    config_reset_sampling_counter();
    
    /* With 50% sample rate, every 2nd allocation should be sampled */
    int sampled_count = 0;
    for (int i = 0; i < 100; i++) {
        if (config_should_sample()) {
            sampled_count++;
        }
    }
    
    /* Should have exactly 50 samples (every 2nd) */
    TEST_ASSERT_EQ(sampled_count, 50,
                   "Deterministic 50% sampling should sample exactly 50 out of 100");
    
    return true;
}

static bool test_deterministic_sampling_reproducible(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "deterministic", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "20", 1);
    config_reload();
    
    /* Run twice and verify results are identical */
    bool results1[50];
    bool results2[50];
    
    config_reset_sampling_counter();
    for (int i = 0; i < 50; i++) {
        results1[i] = config_should_sample();
    }
    
    config_reset_sampling_counter();
    for (int i = 0; i < 50; i++) {
        results2[i] = config_should_sample();
    }
    
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT(results1[i] == results2[i],
                    "Deterministic sampling should be reproducible");
    }
    
    return true;
}

/* ============================================================================
 * Random Sampling Tests
 * ============================================================================ */

static bool test_random_sampling_statistical(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "random", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "50", 1);
    config_reload();
    
    /* Run many iterations and check statistical distribution */
    int sampled_count = 0;
    const int iterations = 10000;
    
    for (int i = 0; i < iterations; i++) {
        if (config_should_sample()) {
            sampled_count++;
        }
    }
    
    /* With 50% rate, expect ~5000 samples. Allow 10% tolerance */
    int expected = iterations * 50 / 100;
    int tolerance = expected / 10;  /* 10% tolerance */
    
    TEST_ASSERT_RANGE(sampled_count, expected - tolerance, expected + tolerance,
                      "Random 50% sampling should be statistically close to 50%");
    
    return true;
}

static bool test_random_sampling_low_rate(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "random", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "10", 1);
    config_reload();
    
    int sampled_count = 0;
    const int iterations = 10000;
    
    for (int i = 0; i < iterations; i++) {
        if (config_should_sample()) {
            sampled_count++;
        }
    }
    
    /* With 10% rate, expect ~1000 samples. Allow 15% tolerance */
    int expected = iterations * 10 / 100;
    int tolerance = expected * 15 / 100;
    
    TEST_ASSERT_RANGE(sampled_count, expected - tolerance, expected + tolerance,
                      "Random 10% sampling should be statistically close to 10%");
    
    return true;
}

static bool test_random_sampling_not_deterministic(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "random", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "50", 1);
    config_reload();
    
    /* Run twice and verify results are likely different */
    bool results1[100];
    bool results2[100];
    
    for (int i = 0; i < 100; i++) {
        results1[i] = config_should_sample();
    }
    
    for (int i = 0; i < 100; i++) {
        results2[i] = config_should_sample();
    }
    
    /* Count differences */
    int differences = 0;
    for (int i = 0; i < 100; i++) {
        if (results1[i] != results2[i]) {
            differences++;
        }
    }
    
    /* With random sampling, we expect some differences (not identical sequences) */
    TEST_ASSERT(differences > 5,
                "Random sampling should produce different results on subsequent runs");
    
    return true;
}

/* ============================================================================
 * 100% Sampling Tests
 * ============================================================================ */

static bool test_100_percent_sampling(void) {
    setenv("MEMROGUE_SAMPLE_RATE", "100", 1);
    config_reload();
    
    /* 100% should always sample, regardless of mode */
    int sampled_count = 0;
    for (int i = 0; i < 100; i++) {
        if (config_should_sample()) {
            sampled_count++;
        }
    }
    
    TEST_ASSERT_EQ(sampled_count, 100,
                   "100% sampling should sample all allocations");
    
    return true;
}

/* ============================================================================
 * Statistics Extrapolation Tests
 * ============================================================================ */

static bool test_extrapolation_100_percent(void) {
    memory_tracker_t* tracker = tracker_create();
    TEST_ASSERT(tracker != NULL, "Tracker creation failed");
    
    /* Track some allocations */
    void* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = malloc(100);
        track_allocation(tracker, ptrs[i], 100, __FILE__, __LINE__);
    }
    
    /* Get extrapolated stats at 100% */
    tracker_stats_t stats;
    tracker_get_extrapolated_stats(tracker, &stats, 100);
    
    /* At 100%, estimated should equal actual */
    TEST_ASSERT_EQ(stats.estimated_total_allocations, stats.total_allocations,
                   "At 100% rate, estimated should equal actual allocations");
    TEST_ASSERT_EQ(stats.estimated_total_bytes, stats.total_bytes_allocated,
                   "At 100% rate, estimated should equal actual bytes");
    
    /* Cleanup */
    for (int i = 0; i < 10; i++) {
        track_deallocation(tracker, ptrs[i]);
        free(ptrs[i]);
    }
    tracker_destroy(tracker);
    
    return true;
}

static bool test_extrapolation_10_percent(void) {
    memory_tracker_t* tracker = tracker_create();
    TEST_ASSERT(tracker != NULL, "Tracker creation failed");
    
    /* Track 10 allocations */
    void* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = malloc(100);
        track_allocation(tracker, ptrs[i], 100, __FILE__, __LINE__);
    }
    
    /* Get extrapolated stats as if these were 10% of total */
    tracker_stats_t stats;
    tracker_get_extrapolated_stats(tracker, &stats, 10);
    
    /* At 10%, estimated should be 10x actual */
    TEST_ASSERT_EQ(stats.estimated_total_allocations, 100,
                   "At 10% rate, estimated allocations should be 10x");
    TEST_ASSERT_EQ(stats.estimated_total_bytes, 10000,
                   "At 10% rate, estimated bytes should be 10x");
    
    /* Cleanup */
    for (int i = 0; i < 10; i++) {
        track_deallocation(tracker, ptrs[i]);
        free(ptrs[i]);
    }
    tracker_destroy(tracker);
    
    return true;
}

static bool test_extrapolation_1_percent(void) {
    memory_tracker_t* tracker = tracker_create();
    TEST_ASSERT(tracker != NULL, "Tracker creation failed");
    
    /* Track 1 allocation */
    void* ptr = malloc(1000);
    track_allocation(tracker, ptr, 1000, __FILE__, __LINE__);
    
    /* Get extrapolated stats as if this was 1% of total */
    tracker_stats_t stats;
    tracker_get_extrapolated_stats(tracker, &stats, 1);
    
    /* At 1%, estimated should be 100x actual */
    TEST_ASSERT_EQ(stats.estimated_total_allocations, 100,
                   "At 1% rate, estimated allocations should be 100x");
    TEST_ASSERT_EQ(stats.estimated_total_bytes, 100000,
                   "At 1% rate, estimated bytes should be 100x");
    
    /* Cleanup */
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
    
    return true;
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

static void* thread_sampling_worker(void* arg) {
    (void)arg;
    
    int local_sampled = 0;
    for (int i = 0; i < 1000; i++) {
        if (config_should_sample()) {
            local_sampled++;
        }
    }
    
    return (void*)(intptr_t)local_sampled;
}

static bool test_sampling_thread_safety(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "random", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "50", 1);
    config_reload();
    
    const int num_threads = 4;
    pthread_t threads[num_threads];
    int threads_created = 0;
    
    /* Create threads */
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_sampling_worker, NULL) == 0) {
            threads_created++;
        }
    }
    
    /* Wait for threads and collect results */
    int total_sampled = 0;
    for (int i = 0; i < threads_created; i++) {
        void* result;
        pthread_join(threads[i], &result);
        total_sampled += (int)(intptr_t)result;
    }
    
    /* Each thread does 1000 iterations at 50%, should get ~500 each */
    /* Total should be ~2000 (4 threads * 500) */
    int expected_total = threads_created * 1000 * 50 / 100;
    int tolerance = expected_total / 5;  /* 20% tolerance for thread variation */
    
    TEST_ASSERT_RANGE(total_sampled, expected_total - tolerance, expected_total + tolerance,
                      "Multi-threaded sampling should maintain statistical properties");
    
    return true;
}

static void* thread_deterministic_worker(void* arg) {
    int thread_id = (int)(intptr_t)arg;
    (void)thread_id;
    
    /* Each thread has its own counter, should get deterministic results */
    config_reset_sampling_counter();
    
    int local_sampled = 0;
    for (int i = 0; i < 100; i++) {
        if (config_should_sample()) {
            local_sampled++;
        }
    }
    
    return (void*)(intptr_t)local_sampled;
}

static bool test_deterministic_thread_independence(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "deterministic", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "10", 1);
    config_reload();
    
    const int num_threads = 4;
    pthread_t threads[num_threads];
    int threads_created = 0;
    
    /* Create threads */
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_deterministic_worker, (void*)(intptr_t)i) == 0) {
            threads_created++;
        }
    }
    
    /* Wait for threads and verify each got 10 samples (10% of 100) */
    for (int i = 0; i < threads_created; i++) {
        void* result;
        pthread_join(threads[i], &result);
        int sampled = (int)(intptr_t)result;
        
        TEST_ASSERT_EQ(sampled, 10,
                       "Each thread should get exactly 10% samples in deterministic mode");
    }
    
    return true;
}

/* ============================================================================
 * API Query Tests
 * ============================================================================ */

static bool test_config_get_sampling_mode(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "deterministic", 1);
    config_reload();
    
    TEST_ASSERT_EQ(config_get_sampling_mode(), MEMROGUE_SAMPLING_DETERMINISTIC,
                   "config_get_sampling_mode should return deterministic");
    
    setenv("MEMROGUE_SAMPLING_MODE", "random", 1);
    config_reload();
    
    TEST_ASSERT_EQ(config_get_sampling_mode(), MEMROGUE_SAMPLING_RANDOM,
                   "config_get_sampling_mode should return random");
    
    return true;
}

static bool test_config_get_sample_rate(void) {
    setenv("MEMROGUE_SAMPLE_RATE", "25", 1);
    config_reload();
    
    TEST_ASSERT_EQ(config_get_sample_rate(), 25,
                   "config_get_sample_rate should return 25");
    
    setenv("MEMROGUE_SAMPLE_RATE", "75", 1);
    config_reload();
    
    TEST_ASSERT_EQ(config_get_sample_rate(), 75,
                   "config_get_sample_rate should return 75");
    
    return true;
}

static bool test_config_reset_sampling_counter(void) {
    setenv("MEMROGUE_SAMPLING_MODE", "deterministic", 1);
    setenv("MEMROGUE_SAMPLE_RATE", "50", 1);
    config_reload();
    
    /* First call should sample */
    config_reset_sampling_counter();
    bool first_result = config_should_sample();
    
    /* Reset and call again - should get same result */
    config_reset_sampling_counter();
    bool second_result = config_should_sample();
    
    TEST_ASSERT(first_result == second_result,
                "Reset counter should allow reproducing sequence");
    
    return true;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    fprintf(stderr, "=== MEMRO-21: Sampling Mode Tests ===\n\n");
    
    /* Initialize config */
    config_load();
    
    /* Sampling Mode Enum Tests */
    fprintf(stderr, "--- Sampling Mode Enum Tests ---\n");
    RUN_TEST(test_sampling_mode_default);
    RUN_TEST(test_sampling_mode_env_random);
    RUN_TEST(test_sampling_mode_env_deterministic);
    RUN_TEST(test_sampling_mode_env_invalid);
    
    /* Deterministic Sampling Tests */
    fprintf(stderr, "\n--- Deterministic Sampling Tests ---\n");
    RUN_TEST(test_deterministic_sampling_10_percent);
    RUN_TEST(test_deterministic_sampling_1_percent);
    RUN_TEST(test_deterministic_sampling_50_percent);
    RUN_TEST(test_deterministic_sampling_reproducible);
    
    /* Random Sampling Tests */
    fprintf(stderr, "\n--- Random Sampling Tests ---\n");
    RUN_TEST(test_random_sampling_statistical);
    RUN_TEST(test_random_sampling_low_rate);
    RUN_TEST(test_random_sampling_not_deterministic);
    
    /* 100% Sampling Tests */
    fprintf(stderr, "\n--- 100%% Sampling Tests ---\n");
    RUN_TEST(test_100_percent_sampling);
    
    /* Statistics Extrapolation Tests */
    fprintf(stderr, "\n--- Statistics Extrapolation Tests ---\n");
    RUN_TEST(test_extrapolation_100_percent);
    RUN_TEST(test_extrapolation_10_percent);
    RUN_TEST(test_extrapolation_1_percent);
    
    /* Thread Safety Tests */
    fprintf(stderr, "\n--- Thread Safety Tests ---\n");
    RUN_TEST(test_sampling_thread_safety);
    RUN_TEST(test_deterministic_thread_independence);
    
    /* API Query Tests */
    fprintf(stderr, "\n--- API Query Tests ---\n");
    RUN_TEST(test_config_get_sampling_mode);
    RUN_TEST(test_config_get_sample_rate);
    RUN_TEST(test_config_reset_sampling_counter);
    
    /* Print summary */
    fprintf(stderr, "\n=== Test Summary ===\n");
    fprintf(stderr, "Total: %d, Passed: %d, Failed: %d\n",
            g_tests_run, g_tests_passed, g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}
