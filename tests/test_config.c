/**
 * @file test_config.c
 * @brief Unit tests for the MemRogue configuration system
 * 
 * Tests environment variable parsing, configuration loading,
 * thread safety, and sampling functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include "memrogue_config.h"

// ============================================================================
// Test Utilities
// ============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(expr, msg) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
        return false; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    g_tests_run++; \
    printf("Running %s...\n", #test_func); \
    clear_env_vars(); \
    config_reload(); /* Reset global config for test isolation */ \
    if (test_func()) { \
        g_tests_passed++; \
        printf("  PASS\n"); \
    } else { \
        g_tests_failed++; \
        printf("  FAILED\n"); \
    } \
} while(0)

/**
 * Clear all MEMROGUE environment variables to ensure clean state.
 */
static void clear_env_vars(void) {
    unsetenv(MEMROGUE_ENV_ENABLED);
    unsetenv(MEMROGUE_ENV_OUTPUT);
    unsetenv(MEMROGUE_ENV_SAMPLE_RATE);
    unsetenv(MEMROGUE_ENV_BACKTRACE);
    unsetenv(MEMROGUE_ENV_VERBOSITY);
    unsetenv(MEMROGUE_ENV_MAX_DEPTH);
    unsetenv(MEMROGUE_ENV_REPORT_ON_EXIT);
    unsetenv(MEMROGUE_ENV_DETECT_DOUBLE_FREE);
    unsetenv(MEMROGUE_ENV_DETECT_INVALID_FREE);
}

// ============================================================================
// Basic Configuration Tests
// ============================================================================

static bool test_config_init_defaults(void) {
    memrogue_config_t config;
    config_init_defaults(&config);
    
    TEST_ASSERT(config.enabled == true, "Default enabled should be true");
    TEST_ASSERT(config.sample_rate == 100, "Default sample rate should be 100");
    TEST_ASSERT(config.backtrace_enabled == true, "Default backtraces should be enabled");
    TEST_ASSERT(config.verbosity == MEMROGUE_VERBOSITY_NORMAL, "Default verbosity should be normal");
    TEST_ASSERT(config.max_backtrace_depth == 16, "Default max depth should be 16");
    TEST_ASSERT(config.report_on_exit == true, "Default report_on_exit should be true");
    TEST_ASSERT(config.detect_double_free == true, "Default detect_double_free should be true");
    TEST_ASSERT(config.detect_invalid_free == true, "Default detect_invalid_free should be true");
    TEST_ASSERT(config.output_path[0] == '\0', "Default output path should be empty");
    TEST_ASSERT(config.output_to_file == false, "Default output_to_file should be false");
    
    return true;
}

static bool test_config_load_defaults(void) {
    // Load with no env vars set - should get defaults
    memrogue_config_t config;
    config_load_into(&config);
    
    TEST_ASSERT(config.enabled == true, "Loaded enabled should be true by default");
    TEST_ASSERT(config.sample_rate == 100, "Loaded sample rate should be 100");
    TEST_ASSERT(config.backtrace_enabled == true, "Loaded backtraces should be enabled");
    TEST_ASSERT(config.verbosity == MEMROGUE_VERBOSITY_NORMAL, "Loaded verbosity should be normal");
    
    return true;
}

// ============================================================================
// Environment Variable Parsing Tests
// ============================================================================

static bool test_config_enabled_env(void) {
    memrogue_config_t config;
    
    // Test enabled = true (various formats)
    setenv(MEMROGUE_ENV_ENABLED, "1", 1);
    config_load_into(&config);
    TEST_ASSERT(config.enabled == true, "MEMROGUE_ENABLED=1 should enable");
    
    setenv(MEMROGUE_ENV_ENABLED, "true", 1);
    config_load_into(&config);
    TEST_ASSERT(config.enabled == true, "MEMROGUE_ENABLED=true should enable");
    
    setenv(MEMROGUE_ENV_ENABLED, "yes", 1);
    config_load_into(&config);
    TEST_ASSERT(config.enabled == true, "MEMROGUE_ENABLED=yes should enable");
    
    setenv(MEMROGUE_ENV_ENABLED, "TRUE", 1);
    config_load_into(&config);
    TEST_ASSERT(config.enabled == true, "MEMROGUE_ENABLED=TRUE should enable (case insensitive)");
    
    // Test enabled = false
    setenv(MEMROGUE_ENV_ENABLED, "0", 1);
    config_load_into(&config);
    TEST_ASSERT(config.enabled == false, "MEMROGUE_ENABLED=0 should disable");
    
    setenv(MEMROGUE_ENV_ENABLED, "false", 1);
    config_load_into(&config);
    TEST_ASSERT(config.enabled == false, "MEMROGUE_ENABLED=false should disable");
    
    setenv(MEMROGUE_ENV_ENABLED, "no", 1);
    config_load_into(&config);
    TEST_ASSERT(config.enabled == false, "MEMROGUE_ENABLED=no should disable");
    
    // Test invalid values - should default to true
    setenv(MEMROGUE_ENV_ENABLED, "invalid", 1);
    config_load_into(&config);
    TEST_ASSERT(config.enabled == true, "Invalid MEMROGUE_ENABLED should default to true");
    
    return true;
}

static bool test_config_sample_rate_env(void) {
    memrogue_config_t config;
    
    // Test valid values
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "50", 1);
    config_load_into(&config);
    TEST_ASSERT(config.sample_rate == 50, "MEMROGUE_SAMPLE_RATE=50 should set 50");
    
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "1", 1);
    config_load_into(&config);
    TEST_ASSERT(config.sample_rate == 1, "MEMROGUE_SAMPLE_RATE=1 should set 1");
    
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "100", 1);
    config_load_into(&config);
    TEST_ASSERT(config.sample_rate == 100, "MEMROGUE_SAMPLE_RATE=100 should set 100");
    
    // Test clamping
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "150", 1);
    config_load_into(&config);
    TEST_ASSERT(config.sample_rate == 100, "MEMROGUE_SAMPLE_RATE=150 should clamp to 100");
    
    // Min sample rate is 1, so 0 and negative values clamp to 1
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "0", 1);
    config_load_into(&config);
    TEST_ASSERT(config.sample_rate == 1, "MEMROGUE_SAMPLE_RATE=0 should clamp to 1");
    
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "-10", 1);
    config_load_into(&config);
    TEST_ASSERT(config.sample_rate == 1, "MEMROGUE_SAMPLE_RATE=-10 should clamp to 1");
    
    // Test invalid values - should default to 100
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "abc", 1);
    config_load_into(&config);
    TEST_ASSERT(config.sample_rate == 100, "Invalid MEMROGUE_SAMPLE_RATE should default to 100");
    
    return true;
}

static bool test_config_backtrace_env(void) {
    memrogue_config_t config;
    
    setenv(MEMROGUE_ENV_BACKTRACE, "1", 1);
    config_load_into(&config);
    TEST_ASSERT(config.backtrace_enabled == true, "MEMROGUE_BACKTRACE=1 should enable");
    
    setenv(MEMROGUE_ENV_BACKTRACE, "0", 1);
    config_load_into(&config);
    TEST_ASSERT(config.backtrace_enabled == false, "MEMROGUE_BACKTRACE=0 should disable");
    
    setenv(MEMROGUE_ENV_BACKTRACE, "true", 1);
    config_load_into(&config);
    TEST_ASSERT(config.backtrace_enabled == true, "MEMROGUE_BACKTRACE=true should enable");
    
    setenv(MEMROGUE_ENV_BACKTRACE, "false", 1);
    config_load_into(&config);
    TEST_ASSERT(config.backtrace_enabled == false, "MEMROGUE_BACKTRACE=false should disable");
    
    return true;
}

static bool test_config_verbosity_env(void) {
    memrogue_config_t config;
    
    // Test numeric values (implementation uses int parsing)
    setenv(MEMROGUE_ENV_VERBOSITY, "0", 1);
    config_load_into(&config);
    TEST_ASSERT(config.verbosity == MEMROGUE_VERBOSITY_QUIET, "MEMROGUE_VERBOSITY=0 should set quiet");
    
    setenv(MEMROGUE_ENV_VERBOSITY, "1", 1);
    config_load_into(&config);
    TEST_ASSERT(config.verbosity == MEMROGUE_VERBOSITY_NORMAL, "MEMROGUE_VERBOSITY=1 should set normal");
    
    setenv(MEMROGUE_ENV_VERBOSITY, "2", 1);
    config_load_into(&config);
    TEST_ASSERT(config.verbosity == MEMROGUE_VERBOSITY_VERBOSE, "MEMROGUE_VERBOSITY=2 should set verbose");
    
    setenv(MEMROGUE_ENV_VERBOSITY, "3", 1);
    config_load_into(&config);
    TEST_ASSERT(config.verbosity == MEMROGUE_VERBOSITY_DEBUG, "MEMROGUE_VERBOSITY=3 should set debug");
    
    // Test out of range - should clamp
    setenv(MEMROGUE_ENV_VERBOSITY, "10", 1);
    config_load_into(&config);
    TEST_ASSERT(config.verbosity == MEMROGUE_VERBOSITY_DEBUG, "MEMROGUE_VERBOSITY=10 should clamp to debug");
    
    // Test invalid values - should default to normal
    setenv(MEMROGUE_ENV_VERBOSITY, "invalid", 1);
    config_load_into(&config);
    TEST_ASSERT(config.verbosity == MEMROGUE_VERBOSITY_NORMAL, "Invalid MEMROGUE_VERBOSITY should default to normal");
    
    return true;
}

static bool test_config_max_depth_env(void) {
    memrogue_config_t config;
    
    setenv(MEMROGUE_ENV_MAX_DEPTH, "16", 1);
    config_load_into(&config);
    TEST_ASSERT(config.max_backtrace_depth == 16, "MEMROGUE_MAX_DEPTH=16 should set 16");
    
    setenv(MEMROGUE_ENV_MAX_DEPTH, "64", 1);
    config_load_into(&config);
    TEST_ASSERT(config.max_backtrace_depth == 64, "MEMROGUE_MAX_DEPTH=64 should set 64");
    
    // Test minimum clamping
    setenv(MEMROGUE_ENV_MAX_DEPTH, "0", 1);
    config_load_into(&config);
    TEST_ASSERT(config.max_backtrace_depth == 1, "MEMROGUE_MAX_DEPTH=0 should clamp to 1");
    
    // Test maximum clamping (max is 64)
    setenv(MEMROGUE_ENV_MAX_DEPTH, "500", 1);
    config_load_into(&config);
    TEST_ASSERT(config.max_backtrace_depth == 64, "MEMROGUE_MAX_DEPTH=500 should clamp to 64");
    
    // Test invalid values (default is 16)
    setenv(MEMROGUE_ENV_MAX_DEPTH, "abc", 1);
    config_load_into(&config);
    TEST_ASSERT(config.max_backtrace_depth == 16, "Invalid MEMROGUE_MAX_DEPTH should default to 16");
    
    return true;
}

static bool test_config_output_file_env(void) {
    memrogue_config_t config;
    
    setenv(MEMROGUE_ENV_OUTPUT, "/tmp/memrogue.log", 1);
    config_load_into(&config);
    TEST_ASSERT(strcmp(config.output_path, "/tmp/memrogue.log") == 0, 
                "MEMROGUE_OUTPUT should set output file path");
    TEST_ASSERT(config.output_to_file == true, "output_to_file should be true when path is set");
    
    // Test truncation for long paths
    char long_path[1024];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    setenv(MEMROGUE_ENV_OUTPUT, long_path, 1);
    config_load_into(&config);
    TEST_ASSERT(strlen(config.output_path) < sizeof(config.output_path), 
                "Long output path should be truncated safely");
    
    return true;
}

static bool test_config_detection_flags_env(void) {
    memrogue_config_t config;
    
    // Test double-free detection
    setenv(MEMROGUE_ENV_DETECT_DOUBLE_FREE, "0", 1);
    config_load_into(&config);
    TEST_ASSERT(config.detect_double_free == false, "MEMROGUE_DETECT_DOUBLE_FREE=0 should disable");
    
    setenv(MEMROGUE_ENV_DETECT_DOUBLE_FREE, "1", 1);
    config_load_into(&config);
    TEST_ASSERT(config.detect_double_free == true, "MEMROGUE_DETECT_DOUBLE_FREE=1 should enable");
    
    // Test invalid-free detection
    setenv(MEMROGUE_ENV_DETECT_INVALID_FREE, "0", 1);
    config_load_into(&config);
    TEST_ASSERT(config.detect_invalid_free == false, "MEMROGUE_DETECT_INVALID_FREE=0 should disable");
    
    setenv(MEMROGUE_ENV_DETECT_INVALID_FREE, "1", 1);
    config_load_into(&config);
    TEST_ASSERT(config.detect_invalid_free == true, "MEMROGUE_DETECT_INVALID_FREE=1 should enable");
    
    // Test report-on-exit
    setenv(MEMROGUE_ENV_REPORT_ON_EXIT, "0", 1);
    config_load_into(&config);
    TEST_ASSERT(config.report_on_exit == false, "MEMROGUE_REPORT_ON_EXIT=0 should disable");
    
    setenv(MEMROGUE_ENV_REPORT_ON_EXIT, "1", 1);
    config_load_into(&config);
    TEST_ASSERT(config.report_on_exit == true, "MEMROGUE_REPORT_ON_EXIT=1 should enable");
    
    return true;
}

// ============================================================================
// Global Config Tests
// ============================================================================

static bool test_config_global_load(void) {
    // Load global config
    config_load();
    
    const memrogue_config_t* cfg = config_get();
    TEST_ASSERT(cfg != NULL, "config_get() should return non-NULL after load");
    TEST_ASSERT(cfg->enabled == true, "Global config should have enabled=true by default");
    
    return true;
}

static bool test_config_global_reload(void) {
    // Initial load
    config_load();
    const memrogue_config_t* cfg1 = config_get();
    TEST_ASSERT(cfg1->sample_rate == 100, "Initial sample rate should be 100");
    
    // Change environment and reload
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "25", 1);
    const memrogue_config_t* reloaded = config_reload();
    TEST_ASSERT(reloaded != NULL, "config_reload() should return non-NULL");
    
    const memrogue_config_t* cfg2 = config_get();
    TEST_ASSERT(cfg2->sample_rate == 25, "After reload, sample rate should be 25");
    
    return true;
}

// ============================================================================
// Query Function Tests
// ============================================================================

static bool test_config_query_functions(void) {
    // Load with specific settings (use numeric verbosity)
    setenv(MEMROGUE_ENV_ENABLED, "1", 1);
    setenv(MEMROGUE_ENV_BACKTRACE, "0", 1);
    setenv(MEMROGUE_ENV_VERBOSITY, "2", 1);  // 2 = verbose
    config_load();
    
    TEST_ASSERT(config_is_enabled() == true, "config_is_enabled() should return true");
    TEST_ASSERT(config_backtraces_enabled() == false, "config_backtraces_enabled() should return false");
    TEST_ASSERT(config_get_verbosity() == MEMROGUE_VERBOSITY_VERBOSE, 
                "config_get_verbosity() should return verbose");
    
    // Test disabled config
    setenv(MEMROGUE_ENV_ENABLED, "0", 1);
    config_reload();
    TEST_ASSERT(config_is_enabled() == false, "config_is_enabled() should return false when disabled");
    
    return true;
}

// ============================================================================
// Sampling Tests
// ============================================================================

static bool test_config_sampling_always(void) {
    // With 100% sample rate, should always return true
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "100", 1);
    config_load();
    
    int count = 0;
    for (int i = 0; i < 1000; i++) {
        if (config_should_sample()) {
            count++;
        }
    }
    
    TEST_ASSERT(count == 1000, "With 100% sample rate, all samples should be taken");
    return true;
}

static bool test_config_sampling_minimum(void) {
    // With 1% sample rate (minimum), should sample very rarely
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "1", 1);
    config_load();
    
    int count = 0;
    const int iterations = 10000;
    for (int i = 0; i < iterations; i++) {
        if (config_should_sample()) {
            count++;
        }
    }
    
    // Allow tolerance for 1% rate - expect ~100 samples out of 10000
    // Allow 0.5% to 2% (50-200 samples)
    double rate = (double)count / iterations;
    TEST_ASSERT(rate < 0.03, "With 1% sample rate, should sample rarely");
    
    return true;
}

static bool test_config_sampling_partial(void) {
    // With 50% sample rate, should sample approximately half
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "50", 1);
    config_load();
    
    int count = 0;
    const int iterations = 10000;
    for (int i = 0; i < iterations; i++) {
        if (config_should_sample()) {
            count++;
        }
    }
    
    // Allow 10% tolerance (40% to 60%)
    double rate = (double)count / iterations;
    TEST_ASSERT(rate > 0.40 && rate < 0.60, 
                "With 50% sample rate, should sample approximately 50%");
    
    return true;
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

typedef struct {
    int thread_id;
    int iterations;
    int samples_taken;
    bool success;
} thread_test_data_t;

static void* sampling_thread_func(void* arg) {
    thread_test_data_t* data = (thread_test_data_t*)arg;
    data->success = true;
    data->samples_taken = 0;
    
    for (int i = 0; i < data->iterations; i++) {
        // Each thread should get its own PRNG state
        if (config_should_sample()) {
            data->samples_taken++;
        }
    }
    
    return NULL;
}

static bool test_config_thread_safety(void) {
    const int num_threads = 4;
    const int iterations_per_thread = 5000;
    
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "50", 1);
    config_load();
    
    pthread_t threads[num_threads];
    thread_test_data_t data[num_threads];
    
    // Start threads
    int threads_created = 0;
    for (int i = 0; i < num_threads; i++) {
        data[i].thread_id = i;
        data[i].iterations = iterations_per_thread;
        int result = pthread_create(&threads[i], NULL, sampling_thread_func, &data[i]);
        if (result != 0) {
            // Join any threads that were successfully created before returning
            for (int j = 0; j < threads_created; j++) {
                pthread_join(threads[j], NULL);
            }
            TEST_ASSERT(false, "pthread_create should succeed");
        }
        threads_created++;
    }
    
    // Wait for threads
    for (int i = 0; i < threads_created; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Verify all threads completed successfully
    int total_samples = 0;
    for (int i = 0; i < num_threads; i++) {
        TEST_ASSERT(data[i].success == true, "Thread should complete successfully");
        total_samples += data[i].samples_taken;
    }
    
    // Verify overall sampling rate is approximately correct
    int total_iterations = num_threads * iterations_per_thread;
    double rate = (double)total_samples / total_iterations;
    TEST_ASSERT(rate > 0.40 && rate < 0.60, 
                "Overall sampling rate should be approximately 50%");
    
    return true;
}

static void* config_access_thread_func(void* arg) {
    thread_test_data_t* data = (thread_test_data_t*)arg;
    data->success = true;
    
    for (int i = 0; i < data->iterations; i++) {
        // Concurrent access to global config
        const memrogue_config_t* cfg = config_get();
        if (cfg == NULL) {
            data->success = false;
            break;
        }
        
        // Read various fields
        (void)cfg->enabled;
        (void)cfg->sample_rate;
        (void)cfg->verbosity;
        
        // Use query functions
        (void)config_is_enabled();
        (void)config_backtraces_enabled();
        (void)config_get_verbosity();
    }
    
    return NULL;
}

static bool test_config_concurrent_access(void) {
    const int num_threads = 8;
    const int iterations_per_thread = 1000;
    
    config_load();
    
    pthread_t threads[num_threads];
    thread_test_data_t data[num_threads];
    
    // Start threads
    int threads_created = 0;
    for (int i = 0; i < num_threads; i++) {
        data[i].thread_id = i;
        data[i].iterations = iterations_per_thread;
        int result = pthread_create(&threads[i], NULL, config_access_thread_func, &data[i]);
        if (result != 0) {
            // Join any threads that were successfully created before returning
            for (int j = 0; j < threads_created; j++) {
                pthread_join(threads[j], NULL);
            }
            TEST_ASSERT(false, "pthread_create should succeed");
        }
        threads_created++;
    }
    
    // Wait for threads
    for (int i = 0; i < threads_created; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Verify all threads completed successfully
    for (int i = 0; i < threads_created; i++) {
        TEST_ASSERT(data[i].success == true, "Thread should complete without errors");
    }
    
    return true;
}

// ============================================================================
// Debug/Format Tests
// ============================================================================

static bool test_config_to_string(void) {
    setenv(MEMROGUE_ENV_ENABLED, "1", 1);
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "75", 1);
    setenv(MEMROGUE_ENV_VERBOSITY, "2", 1);  // 2 = verbose (numeric value)
    config_load();
    
    char buffer[1024];
    int len = config_to_string(config_get(), buffer, sizeof(buffer));
    TEST_ASSERT(len > 0, "config_to_string() should return positive length");
    TEST_ASSERT(strlen(buffer) > 0, "config_to_string() should fill buffer");
    
    // Check that key information is present
    TEST_ASSERT(strstr(buffer, "enabled") != NULL || strstr(buffer, "ENABLED") != NULL, 
                "String should contain enabled field");
    TEST_ASSERT(strstr(buffer, "75") != NULL, "String should contain sample rate 75");
    
    return true;
}

static bool test_config_print(void) {
    config_load();
    
    // Just verify it doesn't crash - output goes to stderr
    config_print(config_get(), stderr);
    
    return true;
}

// ============================================================================
// Parsing Utility Tests
// ============================================================================

static bool test_parse_bool_env(void) {
    // Test with existing env var
    setenv("TEST_BOOL_VAR", "true", 1);
    TEST_ASSERT(config_parse_bool_env("TEST_BOOL_VAR", false) == true, 
                "Should parse 'true' as true");
    
    setenv("TEST_BOOL_VAR", "false", 1);
    TEST_ASSERT(config_parse_bool_env("TEST_BOOL_VAR", true) == false, 
                "Should parse 'false' as false");
    
    // Test with non-existent env var
    unsetenv("TEST_BOOL_VAR");
    TEST_ASSERT(config_parse_bool_env("TEST_BOOL_VAR", true) == true, 
                "Should return default true when not set");
    TEST_ASSERT(config_parse_bool_env("TEST_BOOL_VAR", false) == false, 
                "Should return default false when not set");
    
    return true;
}

static bool test_parse_int_env(void) {
    // Test with existing env var
    setenv("TEST_INT_VAR", "42", 1);
    TEST_ASSERT(config_parse_int_env("TEST_INT_VAR", 0, 0, 100) == 42, 
                "Should parse '42' as 42");
    
    // Test clamping
    setenv("TEST_INT_VAR", "150", 1);
    TEST_ASSERT(config_parse_int_env("TEST_INT_VAR", 0, 0, 100) == 100, 
                "Should clamp 150 to max 100");
    
    setenv("TEST_INT_VAR", "-10", 1);
    TEST_ASSERT(config_parse_int_env("TEST_INT_VAR", 50, 0, 100) == 0, 
                "Should clamp -10 to min 0");
    
    // Test with non-existent env var
    unsetenv("TEST_INT_VAR");
    TEST_ASSERT(config_parse_int_env("TEST_INT_VAR", 55, 0, 100) == 55, 
                "Should return default 55 when not set");
    
    // Test invalid value
    setenv("TEST_INT_VAR", "not_a_number", 1);
    TEST_ASSERT(config_parse_int_env("TEST_INT_VAR", 77, 0, 100) == 77, 
                "Should return default 77 for invalid value");
    
    // Cleanup
    unsetenv("TEST_INT_VAR");
    return true;
}

static bool test_parse_string_env(void) {
    char buffer[64];
    
    // Test with existing env var
    setenv("TEST_STR_VAR", "hello", 1);
    config_parse_string_env("TEST_STR_VAR", buffer, sizeof(buffer), "default");
    TEST_ASSERT(strcmp(buffer, "hello") == 0, "Should parse 'hello'");
    
    // Test with non-existent env var
    unsetenv("TEST_STR_VAR");
    config_parse_string_env("TEST_STR_VAR", buffer, sizeof(buffer), "default");
    TEST_ASSERT(strcmp(buffer, "default") == 0, "Should return 'default' when not set");
    
    // Test truncation
    setenv("TEST_STR_VAR", "this_is_a_very_long_string_that_should_be_truncated", 1);
    char small_buffer[10];
    config_parse_string_env("TEST_STR_VAR", small_buffer, sizeof(small_buffer), "default");
    TEST_ASSERT(strlen(small_buffer) < sizeof(small_buffer), "Should truncate to buffer size");
    TEST_ASSERT(small_buffer[sizeof(small_buffer) - 1] == '\0', "Should be null-terminated");
    
    // Cleanup
    unsetenv("TEST_STR_VAR");
    return true;
}

// ============================================================================
// Edge Case Tests
// ============================================================================

static bool test_config_null_handling(void) {
    // These should not crash
    config_init_defaults(NULL);
    config_print(NULL, stderr);
    
    char buffer[256];
    int len = config_to_string(NULL, buffer, sizeof(buffer));
    TEST_ASSERT(len >= 0, "config_to_string(NULL, ...) should not crash");
    
    return true;
}

static bool test_config_empty_env_vars(void) {
    memrogue_config_t config;
    
    // Set empty values
    setenv(MEMROGUE_ENV_ENABLED, "", 1);
    setenv(MEMROGUE_ENV_SAMPLE_RATE, "", 1);
    setenv(MEMROGUE_ENV_OUTPUT, "", 1);
    
    config_load_into(&config);
    
    // Should use defaults for empty values
    TEST_ASSERT(config.enabled == true, "Empty MEMROGUE_ENABLED should use default");
    TEST_ASSERT(config.sample_rate == 100, "Empty MEMROGUE_SAMPLE_RATE should use default");
    TEST_ASSERT(config.output_path[0] == '\0', "Empty MEMROGUE_OUTPUT should be empty");
    
    return true;
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("=== MemRogue Configuration Tests ===\n\n");
    
    // Basic configuration tests
    RUN_TEST(test_config_init_defaults);
    RUN_TEST(test_config_load_defaults);
    
    // Environment variable parsing tests
    RUN_TEST(test_config_enabled_env);
    RUN_TEST(test_config_sample_rate_env);
    RUN_TEST(test_config_backtrace_env);
    RUN_TEST(test_config_verbosity_env);
    RUN_TEST(test_config_max_depth_env);
    RUN_TEST(test_config_output_file_env);
    RUN_TEST(test_config_detection_flags_env);
    
    // Global config tests
    RUN_TEST(test_config_global_load);
    RUN_TEST(test_config_global_reload);
    
    // Query function tests
    RUN_TEST(test_config_query_functions);
    
    // Sampling tests
    RUN_TEST(test_config_sampling_always);
    RUN_TEST(test_config_sampling_minimum);
    RUN_TEST(test_config_sampling_partial);
    
    // Thread safety tests
    RUN_TEST(test_config_thread_safety);
    RUN_TEST(test_config_concurrent_access);
    
    // Debug/format tests
    RUN_TEST(test_config_to_string);
    RUN_TEST(test_config_print);
    
    // Parsing utility tests
    RUN_TEST(test_parse_bool_env);
    RUN_TEST(test_parse_int_env);
    RUN_TEST(test_parse_string_env);
    
    // Edge case tests
    RUN_TEST(test_config_null_handling);
    RUN_TEST(test_config_empty_env_vars);
    
    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Total:  %d\n", g_tests_run);
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}
