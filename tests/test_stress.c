/**
 * @file test_stress.c
 * @brief Stress testing for MemRogue memory debugger
 * 
 * MEMRO-28: Stress Testing
 * 
 * This file implements comprehensive stress tests for MemRogue to verify:
 * - High allocation count handling (millions of allocations)
 * - Large allocation size handling
 * - Long-running stability
 * - Memory efficiency of the debugger itself
 * 
 * SAFETY NOTES:
 * - All tests have configurable limits with SAFE DEFAULTS for laptops
 * - Tests use allocate/free cycles to avoid exhausting system memory
 * - Large allocation tests cap at safe limits by default
 * - Duration tests are configurable (not 24 hours by default)
 * 
 * Environment Variables for Configuration:
 *   STRESS_ALLOCATION_COUNT   - Number of allocations (default: 1M, max: 100M)
 *   STRESS_MAX_ALLOC_SIZE     - Maximum allocation size in MB (default: 64MB)
 *   STRESS_DURATION_MINUTES   - Stability test duration (default: 5 minutes)
 *   STRESS_CONCURRENT_ALLOCS  - Max concurrent allocations (default: 10000)
 *   STRESS_VERBOSE            - Enable verbose output (default: 0)
 * 
 * Usage:
 *   # Run with defaults (safe for laptop)
 *   ./bin/test_stress
 * 
 *   # Run with MemRogue
 *   LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/test_stress
 * 
 *   # Custom configuration
 *   STRESS_ALLOCATION_COUNT=5000000 STRESS_DURATION_MINUTES=10 ./bin/test_stress
 * 
 * @author MemRogue Team
 * @date 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include <pthread.h>
#include <locale.h>

/* ============================================================================
 * Configuration Constants and Defaults
 * ============================================================================ */

/* Safe defaults for laptop testing */
#define DEFAULT_ALLOCATION_COUNT     1000000    /* 1 million */
#define DEFAULT_MAX_ALLOC_SIZE_MB    64         /* 64 MB max single allocation */
#define DEFAULT_DURATION_MINUTES     5          /* 5 minute stability test */
#define DEFAULT_CONCURRENT_ALLOCS    10000      /* 10K concurrent allocations */
#define DEFAULT_VERBOSE              0

/* Hard limits for safety */
#define MAX_ALLOCATION_COUNT         100000000  /* 100 million absolute max */
#define MAX_ALLOC_SIZE_MB            1024       /* 1 GB absolute max */
#define MAX_DURATION_MINUTES         1440       /* 24 hours absolute max */
#define MAX_CONCURRENT_ALLOCS        1000000    /* 1 million concurrent max */

/* Test configuration structure */
typedef struct {
    size_t allocation_count;
    size_t max_alloc_size_bytes;
    int duration_minutes;
    size_t concurrent_allocs;
    int verbose;
} stress_config_t;

/* Test results structure */
typedef struct {
    size_t total_allocations;
    size_t total_frees;
    size_t total_bytes_allocated;
    size_t peak_memory_usage;
    double elapsed_seconds;
    size_t allocations_per_second;
    bool passed;
    char error_message[256];
} stress_result_t;

/* Global state for signal handling */
static volatile sig_atomic_t g_stop_requested = 0;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int signum) {
    (void)signum;
    g_stop_requested = 1;
    printf("\n[STRESS] Shutdown requested, finishing current test...\n");
}

/**
 * @brief Get current timestamp in seconds
 */
static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/**
 * @brief Get current memory usage of this process
 */
static size_t get_current_memory_usage(void) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return (size_t)usage.ru_maxrss * 1024;  /* Convert KB to bytes */
    }
    return 0;
}

/**
 * @brief Parse environment variable as size_t with default
 */
static size_t get_env_size_t(const char* name, size_t default_val, size_t max_val) {
    const char* val = getenv(name);
    if (!val) return default_val;
    
    char* endptr;
    unsigned long parsed = strtoul(val, &endptr, 10);
    
    /* Check for parsing errors (invalid characters) */
    if (*endptr != '\0') {
        fprintf(stderr, "[STRESS] Warning: Invalid %s value (non-numeric), using default\n", name);
        return default_val;
    }
    
    /* Explicitly reject zero values as they would cause issues */
    if (parsed == 0) {
        fprintf(stderr, "[STRESS] Warning: %s cannot be zero, using default\n", name);
        return default_val;
    }
    
    if (parsed > max_val) {
        fprintf(stderr, "[STRESS] Warning: %s capped at %zu for safety\n", name, max_val);
        return max_val;
    }
    
    return (size_t)parsed;
}

/**
 * @brief Parse environment variable as int with default
 */
static int get_env_int(const char* name, int default_val, int max_val) {
    const char* val = getenv(name);
    if (!val) return default_val;
    
    char* endptr;
    long parsed = strtol(val, &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "[STRESS] Warning: Invalid %s value, using default\n", name);
        return default_val;
    }
    
    if (parsed > max_val) {
        fprintf(stderr, "[STRESS] Warning: %s capped at %d for safety\n", name, max_val);
        return max_val;
    }
    
    return (int)parsed;
}

/**
 * @brief Load configuration from environment variables
 */
static stress_config_t load_config(void) {
    stress_config_t config;
    
    config.allocation_count = get_env_size_t(
        "STRESS_ALLOCATION_COUNT", 
        DEFAULT_ALLOCATION_COUNT, 
        MAX_ALLOCATION_COUNT
    );
    
    size_t max_mb = get_env_size_t(
        "STRESS_MAX_ALLOC_SIZE", 
        DEFAULT_MAX_ALLOC_SIZE_MB, 
        MAX_ALLOC_SIZE_MB
    );
    config.max_alloc_size_bytes = max_mb * 1024 * 1024;
    
    config.duration_minutes = get_env_int(
        "STRESS_DURATION_MINUTES", 
        DEFAULT_DURATION_MINUTES, 
        MAX_DURATION_MINUTES
    );
    
    config.concurrent_allocs = get_env_size_t(
        "STRESS_CONCURRENT_ALLOCS", 
        DEFAULT_CONCURRENT_ALLOCS, 
        MAX_CONCURRENT_ALLOCS
    );
    
    config.verbose = get_env_int("STRESS_VERBOSE", DEFAULT_VERBOSE, 1);
    
    return config;
}

/**
 * @brief Print configuration
 */
static void print_config(const stress_config_t* config) {
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║              MemRogue Stress Test Configuration                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("  Allocation count:    %zu\n", config->allocation_count);
    printf("  Max alloc size:      %zu MB\n", config->max_alloc_size_bytes / (1024 * 1024));
    printf("  Stability duration:  %d minutes\n", config->duration_minutes);
    printf("  Concurrent allocs:   %zu\n", config->concurrent_allocs);
    printf("  Verbose:             %s\n", config->verbose ? "yes" : "no");
    printf("══════════════════════════════════════════════════════════════════\n\n");
}

/**
 * @brief Format bytes as human-readable string
 */
static const char* format_bytes(size_t bytes, char* buf, size_t buf_size) {
    if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(buf, buf_size, "%.2f GB", (double)bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buf, buf_size, "%.2f MB", (double)bytes / (1024.0 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buf, buf_size, "%.2f KB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, buf_size, "%zu bytes", bytes);
    }
    return buf;
}

/* ============================================================================
 * Test 1: High Allocation Count Test
 * ============================================================================
 * Tests MemRogue's ability to track millions of allocations.
 * Uses allocate/free cycles to avoid memory exhaustion.
 */

static stress_result_t test_high_allocation_count(const stress_config_t* config) {
    stress_result_t result = {0};
    result.passed = true;
    
    printf("\n═══════════════════════════════════════════════════════════════════\n");
    printf("TEST 1: High Allocation Count (%zu allocations)\n", config->allocation_count);
    printf("═══════════════════════════════════════════════════════════════════\n");
    
    /* Use a pool of pointers for cycling allocations */
    size_t pool_size = config->concurrent_allocs;
    void** pool = calloc(pool_size, sizeof(void*));
    if (!pool) {
        result.passed = false;
        snprintf(result.error_message, sizeof(result.error_message),
                 "Failed to allocate pointer pool");
        return result;
    }
    
    size_t alloc_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    size_t num_sizes = sizeof(alloc_sizes) / sizeof(alloc_sizes[0]);
    
    double start_time = get_time_seconds();
    size_t progress_interval = config->allocation_count / 10;
    if (progress_interval == 0) progress_interval = 1;
    
    for (size_t i = 0; i < config->allocation_count && !g_stop_requested; i++) {
        size_t pool_idx = i % pool_size;
        
        /* Free existing allocation at this slot */
        if (pool[pool_idx]) {
            free(pool[pool_idx]);
            result.total_frees++;
        }
        
        /* Allocate new block with varying size */
        size_t size = alloc_sizes[i % num_sizes];
        pool[pool_idx] = malloc(size);
        
        if (!pool[pool_idx]) {
            result.passed = false;
            snprintf(result.error_message, sizeof(result.error_message),
                     "malloc failed at allocation %zu", i);
            break;
        }
        
        /* Touch memory to ensure it's actually allocated */
        memset(pool[pool_idx], (int)(i & 0xFF), size);
        
        result.total_allocations++;
        result.total_bytes_allocated += size;
        
        /* Progress reporting */
        if (config->verbose && (i + 1) % progress_interval == 0) {
            printf("  Progress: %zu/%zu (%.0f%%)\n", 
                   i + 1, config->allocation_count,
                   (double)(i + 1) * 100.0 / (double)config->allocation_count);
        }
    }
    
    /* Final cleanup */
    for (size_t i = 0; i < pool_size; i++) {
        if (pool[i]) {
            free(pool[i]);
            result.total_frees++;
        }
    }
    free(pool);
    
    result.elapsed_seconds = get_time_seconds() - start_time;
    /* Prevent division by zero or extremely small elapsed time */
    if (result.elapsed_seconds > 0.0) {
        result.allocations_per_second = (size_t)((double)result.total_allocations / result.elapsed_seconds);
    } else {
        result.allocations_per_second = 0;
    }
    result.peak_memory_usage = get_current_memory_usage();
    
    char buf[64];
    printf("\n  Results:\n");
    printf("    Total allocations:      %zu\n", result.total_allocations);
    printf("    Total frees:            %zu\n", result.total_frees);
    printf("    Total bytes allocated:  %s\n", format_bytes(result.total_bytes_allocated, buf, sizeof(buf)));
    printf("    Elapsed time:           %.2f seconds\n", result.elapsed_seconds);
    printf("    Allocations/second:     %zu\n", result.allocations_per_second);
    printf("    Status:                 %s\n", result.passed ? "PASSED ✓" : "FAILED ✗");
    
    if (!result.passed) {
        printf("    Error: %s\n", result.error_message);
    }
    
    return result;
}

/* ============================================================================
 * Test 2: Large Allocation Size Test
 * ============================================================================
 * Tests MemRogue's ability to track large allocations.
 * Carefully manages memory to avoid system exhaustion.
 */

static stress_result_t test_large_allocations(const stress_config_t* config) {
    stress_result_t result = {0};
    result.passed = true;
    
    printf("\n═══════════════════════════════════════════════════════════════════\n");
    printf("TEST 2: Large Allocation Sizes (up to %zu MB)\n", 
           config->max_alloc_size_bytes / (1024 * 1024));
    printf("═══════════════════════════════════════════════════════════════════\n");
    
    /* Test allocation sizes: 1KB, 1MB, 10MB, 64MB, etc. */
    size_t test_sizes[] = {
        1024,                          /* 1 KB */
        1024 * 1024,                   /* 1 MB */
        10 * 1024 * 1024,              /* 10 MB */
        64 * 1024 * 1024,              /* 64 MB */
        128 * 1024 * 1024,             /* 128 MB */
        256 * 1024 * 1024,             /* 256 MB */
        512 * 1024 * 1024,             /* 512 MB */
        1024ULL * 1024 * 1024,         /* 1 GB */
    };
    size_t num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    double start_time = get_time_seconds();
    char buf[64];
    
    for (size_t i = 0; i < num_sizes && !g_stop_requested; i++) {
        size_t size = test_sizes[i];
        
        /* Skip sizes larger than configured max */
        if (size > config->max_alloc_size_bytes) {
            printf("  [SKIP] %s (exceeds configured max)\n", 
                   format_bytes(size, buf, sizeof(buf)));
            continue;
        }
        
        printf("  Testing %s allocation...", format_bytes(size, buf, sizeof(buf)));
        fflush(stdout);
        
        void* ptr = malloc(size);
        if (!ptr) {
            printf(" FAILED (malloc returned NULL)\n");
            /* Not a test failure - just system limitation */
            continue;
        }
        
        result.total_allocations++;
        result.total_bytes_allocated += size;
        
        /* Touch memory to ensure it's actually mapped */
        memset(ptr, 0xAB, size);
        
        /* Verify memory is accessible */
        volatile unsigned char* vptr = (volatile unsigned char*)ptr;
        if (vptr[0] != 0xAB || vptr[size - 1] != 0xAB) {
            printf(" FAILED (memory verification)\n");
            result.passed = false;
            snprintf(result.error_message, sizeof(result.error_message),
                     "Memory verification failed for %s allocation",
                     format_bytes(size, buf, sizeof(buf)));
            free(ptr);
            break;
        }
        
        free(ptr);
        result.total_frees++;
        
        printf(" OK ✓\n");
    }
    
    result.elapsed_seconds = get_time_seconds() - start_time;
    result.peak_memory_usage = get_current_memory_usage();
    
    printf("\n  Results:\n");
    printf("    Allocations tested:     %zu\n", result.total_allocations);
    printf("    Total bytes allocated:  %s\n", format_bytes(result.total_bytes_allocated, buf, sizeof(buf)));
    printf("    Elapsed time:           %.2f seconds\n", result.elapsed_seconds);
    printf("    Status:                 %s\n", result.passed ? "PASSED ✓" : "FAILED ✗");
    
    return result;
}

/* ============================================================================
 * Test 3: Stability Test (Long-Running)
 * ============================================================================
 * Tests MemRogue's stability over extended periods.
 * Continuously allocates and frees memory in patterns.
 */

static stress_result_t test_stability(const stress_config_t* config) {
    stress_result_t result = {0};
    result.passed = true;
    
    printf("\n═══════════════════════════════════════════════════════════════════\n");
    printf("TEST 3: Stability Test (%d minutes)\n", config->duration_minutes);
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  Press Ctrl+C to stop early...\n\n");
    
    double duration_seconds = config->duration_minutes * 60.0;
    double start_time = get_time_seconds();
    double last_report_time = start_time;
    double report_interval = 30.0;  /* Report every 30 seconds */
    
    /* Pool for random allocation patterns */
    size_t pool_size = 1000;
    void** pool = calloc(pool_size, sizeof(void*));
    size_t* sizes = calloc(pool_size, sizeof(size_t));
    
    if (!pool || !sizes) {
        result.passed = false;
        snprintf(result.error_message, sizeof(result.error_message),
                 "Failed to allocate test structures");
        free(pool);
        free(sizes);
        return result;
    }
    
    unsigned int seed = (unsigned int)time(NULL);
    size_t iteration = 0;
    
    while (!g_stop_requested) {
        double elapsed = get_time_seconds() - start_time;
        
        if (elapsed >= duration_seconds) {
            break;
        }
        
        /* Random allocation/free pattern */
        size_t idx = (size_t)rand_r(&seed) % pool_size;
        
        if (pool[idx]) {
            /* Free existing */
            free(pool[idx]);
            result.total_frees++;
            pool[idx] = NULL;
        }
        
        /* Allocate with random size (16 bytes to 64KB) */
        size_t size = 16 + ((size_t)rand_r(&seed) % (64 * 1024));
        pool[idx] = malloc(size);
        
        if (!pool[idx]) {
            /* Out of memory - not necessarily a failure, reduce pool */
            continue;
        }
        
        sizes[idx] = size;
        memset(pool[idx], (int)(iteration & 0xFF), size);
        result.total_allocations++;
        result.total_bytes_allocated += size;
        
        iteration++;
        
        /* Periodic status report */
        double now = get_time_seconds();
        if (now - last_report_time >= report_interval) {
            size_t current_memory = get_current_memory_usage();
            char buf[64];
            printf("  [%.0f/%.0f sec] Allocations: %zu, Memory: %s\n",
                   elapsed, duration_seconds,
                   result.total_allocations,
                   format_bytes(current_memory, buf, sizeof(buf)));
            last_report_time = now;
        }
    }
    
    /* Cleanup */
    for (size_t i = 0; i < pool_size; i++) {
        if (pool[i]) {
            free(pool[i]);
            result.total_frees++;
        }
    }
    free(pool);
    free(sizes);
    
    result.elapsed_seconds = get_time_seconds() - start_time;
    /* Prevent division by zero or extremely small elapsed time */
    if (result.elapsed_seconds > 0.0) {
        result.allocations_per_second = (size_t)((double)result.total_allocations / result.elapsed_seconds);
    } else {
        result.allocations_per_second = 0;
    }
    result.peak_memory_usage = get_current_memory_usage();
    
    char buf[64];
    printf("\n  Results:\n");
    printf("    Duration:               %.2f seconds (%.1f minutes)\n", 
           result.elapsed_seconds, result.elapsed_seconds / 60.0);
    printf("    Total allocations:      %zu\n", result.total_allocations);
    printf("    Total frees:            %zu\n", result.total_frees);
    printf("    Allocations/second:     %zu\n", result.allocations_per_second);
    printf("    Peak memory:            %s\n", format_bytes(result.peak_memory_usage, buf, sizeof(buf)));
    printf("    Status:                 %s\n", result.passed ? "PASSED ✓" : "FAILED ✗");
    
    return result;
}

/* ============================================================================
 * Test 4: Multithreaded Stress Test
 * ============================================================================
 * Tests MemRogue's thread safety under high contention.
 */

#define STRESS_NUM_THREADS 4

typedef struct {
    int thread_id;
    size_t allocations_per_thread;
    size_t total_allocated;
    size_t total_freed;
    bool success;
} thread_data_t;

static void* thread_stress_worker(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    data->success = true;
    
    size_t pool_size = 100;
    void** pool = calloc(pool_size, sizeof(void*));
    if (!pool) {
        data->success = false;
        return NULL;
    }
    
    unsigned int seed = (unsigned int)(time(NULL) + data->thread_id);
    
    for (size_t i = 0; i < data->allocations_per_thread && !g_stop_requested; i++) {
        size_t idx = i % pool_size;
        
        if (pool[idx]) {
            free(pool[idx]);
            data->total_freed++;
        }
        
        size_t size = 64 + ((size_t)rand_r(&seed) % 4096);
        pool[idx] = malloc(size);
        
        if (pool[idx]) {
            memset(pool[idx], (int)(i & 0xFF), size);
            data->total_allocated++;
        }
    }
    
    /* Cleanup */
    for (size_t i = 0; i < pool_size; i++) {
        if (pool[i]) {
            free(pool[i]);
            data->total_freed++;
        }
    }
    free(pool);
    
    return NULL;
}

static stress_result_t test_multithreaded(const stress_config_t* config) {
    stress_result_t result = {0};
    result.passed = true;
    
    printf("\n═══════════════════════════════════════════════════════════════════\n");
    printf("TEST 4: Multithreaded Stress Test (%d threads)\n", STRESS_NUM_THREADS);
    printf("═══════════════════════════════════════════════════════════════════\n");
    
    pthread_t threads[STRESS_NUM_THREADS];
    thread_data_t thread_data[STRESS_NUM_THREADS];
    
    size_t allocs_per_thread = config->allocation_count / STRESS_NUM_THREADS;
    
    double start_time = get_time_seconds();
    
    /* Create threads */
    for (int i = 0; i < STRESS_NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].allocations_per_thread = allocs_per_thread;
        thread_data[i].total_allocated = 0;
        thread_data[i].total_freed = 0;
        thread_data[i].success = false;
        
        if (pthread_create(&threads[i], NULL, thread_stress_worker, &thread_data[i]) != 0) {
            result.passed = false;
            snprintf(result.error_message, sizeof(result.error_message),
                     "Failed to create thread %d", i);
            return result;
        }
    }
    
    /* Wait for threads */
    for (int i = 0; i < STRESS_NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        
        result.total_allocations += thread_data[i].total_allocated;
        result.total_frees += thread_data[i].total_freed;
        
        if (!thread_data[i].success) {
            result.passed = false;
        }
        
        if (config->verbose) {
            printf("  Thread %d: %zu allocs, %zu frees\n",
                   i, thread_data[i].total_allocated, thread_data[i].total_freed);
        }
    }
    
    result.elapsed_seconds = get_time_seconds() - start_time;
    /* Prevent division by zero or extremely small elapsed time */
    if (result.elapsed_seconds > 0.0) {
        result.allocations_per_second = (size_t)((double)result.total_allocations / result.elapsed_seconds);
    } else {
        result.allocations_per_second = 0;
    }
    result.peak_memory_usage = get_current_memory_usage();
    
    printf("\n  Results:\n");
    printf("    Threads:                %d\n", STRESS_NUM_THREADS);
    printf("    Total allocations:      %zu\n", result.total_allocations);
    printf("    Total frees:            %zu\n", result.total_frees);
    printf("    Elapsed time:           %.2f seconds\n", result.elapsed_seconds);
    printf("    Allocations/second:     %zu\n", result.allocations_per_second);
    printf("    Status:                 %s\n", result.passed ? "PASSED ✓" : "FAILED ✗");
    
    return result;
}

/* ============================================================================
 * Main Function
 * ============================================================================ */

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    /* Setup signal handler for graceful shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Enable locale for thousands separators */
    setlocale(LC_NUMERIC, "");
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║              MemRogue Stress Testing Suite                       ║\n");
    printf("║              Ensure that it's totally safe                       ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n\n");
    
    printf("SAFETY NOTICE:\n");
    printf("  This test suite has SAFE DEFAULTS for laptop use.\n");
    printf("  Configure via environment variables for heavier testing.\n");
    printf("  Press Ctrl+C to stop any test gracefully.\n\n");
    
    /* Load configuration */
    stress_config_t config = load_config();
    print_config(&config);
    
    /* Track overall results */
    int tests_run = 0;
    int tests_passed = 0;
    stress_result_t results[4] = {{0}, {0}, {0}, {0}};
    
    /* Run tests */
    
    /* Test 1: High allocation count */
    results[0] = test_high_allocation_count(&config);
    tests_run++;
    if (results[0].passed) tests_passed++;
    
    if (g_stop_requested) goto summary;
    
    /* Test 2: Large allocations */
    results[1] = test_large_allocations(&config);
    tests_run++;
    if (results[1].passed) tests_passed++;
    
    if (g_stop_requested) goto summary;
    
    /* Test 3: Stability (only if duration > 0) */
    if (config.duration_minutes > 0) {
        results[2] = test_stability(&config);
        tests_run++;
        if (results[2].passed) tests_passed++;
    }
    
    if (g_stop_requested) goto summary;
    
    /* Test 4: Multithreaded */
    results[3] = test_multithreaded(&config);
    tests_run++;
    if (results[3].passed) tests_passed++;
    
summary:
    /* Print summary */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                        TEST SUMMARY                              ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("  Tests run:     %d\n", tests_run);
    printf("  Tests passed:  %d\n", tests_passed);
    printf("  Tests failed:  %d\n", tests_run - tests_passed);
    printf("  Status:        %s\n", (tests_passed == tests_run) ? "ALL PASSED ✓" : "SOME FAILED ✗");
    
    /* Print capacity documentation */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                    CAPACITY DOCUMENTATION                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    
    if (tests_run >= 1 && results[0].passed) {
        printf("  High Allocation Test:\n");
        printf("    - Handled %zu allocations\n", results[0].total_allocations);
        printf("    - Rate: %zu allocs/sec\n", results[0].allocations_per_second);
    }
    
    if (tests_run >= 2 && results[1].passed) {
        char buf[64];
        printf("  Large Allocation Test:\n");
        printf("    - Max tested: %s\n", format_bytes(results[1].total_bytes_allocated, buf, sizeof(buf)));
    }
    
    if (tests_run >= 3 && results[2].passed) {
        printf("  Stability Test:\n");
        printf("    - Duration: %.1f minutes\n", results[2].elapsed_seconds / 60.0);
        printf("    - Sustained rate: %zu allocs/sec\n", results[2].allocations_per_second);
    }
    
    if (tests_run >= 4 && results[3].passed) {
        printf("  Multithreaded Test:\n");
        printf("    - %d concurrent threads\n", STRESS_NUM_THREADS);
        printf("    - Rate: %zu allocs/sec\n", results[3].allocations_per_second);
    }
    
    printf("\n  Run with LD_PRELOAD=./lib/libmemrogue_intercept.so to test with MemRogue\n");
    printf("══════════════════════════════════════════════════════════════════\n");
    
    return (tests_passed == tests_run) ? 0 : 1;
}
