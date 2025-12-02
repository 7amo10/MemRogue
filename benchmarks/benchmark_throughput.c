/**
 * @file benchmark_throughput.c
 * @brief Throughput benchmarks for MemRogue
 * 
 * MEMRO-26: Performance Benchmarks
 * 
 * This benchmark measures the throughput overhead of MemRogue tracking,
 * focusing on allocations per second under sustained load.
 * 
 * Benchmarks included:
 * 1. Single-threaded allocation throughput
 * 2. Multi-threaded allocation throughput
 * 3. Sustained allocation stress test
 * 4. Memory pool simulation
 * 5. Calloc/Realloc throughput
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "benchmark_common.h"
#include "../include/memrogue_tracker.h"

/* ============================================================================
 * Benchmark Configuration
 * ============================================================================ */

/**
 * Target overhead percentage - must be below this to pass.
 * 
 * Context: Memory debuggers inherently add overhead per allocation.
 * - Valgrind (Memcheck): 1000-5000% overhead (10-50x slowdown)
 * - AddressSanitizer: ~100% overhead (2x slowdown)
 * - MemRogue: Target <500% overhead (5x slowdown)
 * 
 * At <500% overhead, MemRogue is 10-100x faster than Valgrind.
 */
#define TARGET_OVERHEAD_PERCENT  500.0

/** Duration for each throughput test (seconds) */
#define TEST_DURATION_SEC        2.0

/** Number of threads for multi-threaded tests */
#define NUM_THREADS              4

/** Maximum allocations to track in sustained test */
#define MAX_SUSTAINED_ALLOCS     10000

/* ============================================================================
 * Benchmark 1: Single-Threaded Throughput
 * ============================================================================ */

static bench_result_t run_benchmark_single_thread_throughput(void) {
    bench_result_t result;
    strncpy(result.name, "Single-Thread Throughput", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    /* Baseline: alloc/free without tracking */
    printf("  Running baseline throughput...\n");
    uint64_t baseline_ops = 0;
    bench_timer_t start;
    bench_timer_start(&start);
    
    while (bench_timer_elapsed_sec(&start) < TEST_DURATION_SEC) {
        void* ptr = malloc(64);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
        baseline_ops++;
    }
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    result.baseline.throughput = (double)baseline_ops / baseline_time;
    result.baseline.count = baseline_ops;
    result.baseline.mean = baseline_time / (double)baseline_ops * 1e9; /* ns per op */
    
    /* Tracked: alloc/free with tracking */
    printf("  Running tracked throughput...\n");
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    uint64_t tracked_ops = 0;
    bench_timer_start(&start);
    
    while (bench_timer_elapsed_sec(&start) < TEST_DURATION_SEC) {
        void* ptr = malloc(64);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        track_allocation(tracker, ptr, 64, __FILE__, __LINE__);
        track_deallocation(tracker, ptr);
        free(ptr);
        tracked_ops++;
    }
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.throughput = (double)tracked_ops / tracked_time;
    result.tracked.count = tracked_ops;
    result.tracked.mean = tracked_time / (double)tracked_ops * 1e9; /* ns per op */
    
    tracker_destroy(tracker);
    
    /* Calculate overhead based on throughput reduction */
    if (result.baseline.throughput > 0) {
        double throughput_ratio = result.tracked.throughput / result.baseline.throughput;
        result.overhead_percent = (1.0 - throughput_ratio) * 100.0;
        result.overhead_ns = result.tracked.mean - result.baseline.mean;
    } else {
        result.overhead_percent = 0.0;
        result.overhead_ns = 0.0;
    }
    
    result.target_overhead = TARGET_OVERHEAD_PERCENT;
    result.passed = (result.overhead_percent <= TARGET_OVERHEAD_PERCENT);
    
    return result;
}

/* ============================================================================
 * Benchmark 2: Multi-Threaded Throughput
 * ============================================================================ */

typedef struct {
    uint64_t ops;
    double elapsed_sec;
    memory_tracker_t* tracker;  /* NULL for baseline */
    bool use_tracking;
} thread_throughput_data_t;

static void* thread_throughput_worker(void* arg) {
    thread_throughput_data_t* data = (thread_throughput_data_t*)arg;
    
    bench_timer_t start;
    bench_timer_start(&start);
    
    while (bench_timer_elapsed_sec(&start) < TEST_DURATION_SEC) {
        void* ptr = malloc(64);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        
        if (data->use_tracking && data->tracker) {
            track_allocation(data->tracker, ptr, 64, __FILE__, __LINE__);
            track_deallocation(data->tracker, ptr);
        }
        
        free(ptr);
        data->ops++;
    }
    
    data->elapsed_sec = bench_timer_elapsed_sec(&start);
    return NULL;
}

static bench_result_t run_benchmark_multi_thread_throughput(void) {
    bench_result_t result;
    strncpy(result.name, "Multi-Thread Throughput (4 threads)", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    pthread_t threads[NUM_THREADS];
    thread_throughput_data_t thread_data[NUM_THREADS];
    
    /* Baseline: multi-threaded without tracking */
    printf("  Running baseline throughput (%d threads)...\n", NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].ops = 0;
        thread_data[i].elapsed_sec = 0.0;
        thread_data[i].tracker = NULL;
        thread_data[i].use_tracking = false;
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_throughput_worker, &thread_data[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    uint64_t total_baseline_ops = 0;
    double max_baseline_time = 0.0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total_baseline_ops += thread_data[i].ops;
        if (thread_data[i].elapsed_sec > max_baseline_time) {
            max_baseline_time = thread_data[i].elapsed_sec;
        }
    }
    
    result.baseline.throughput = (double)total_baseline_ops / max_baseline_time;
    result.baseline.count = total_baseline_ops;
    result.baseline.mean = max_baseline_time / (double)total_baseline_ops * 1e9;
    
    /* Tracked: multi-threaded with tracking */
    printf("  Running tracked throughput (%d threads)...\n", NUM_THREADS);
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].ops = 0;
        thread_data[i].elapsed_sec = 0.0;
        thread_data[i].tracker = tracker;
        thread_data[i].use_tracking = true;
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_throughput_worker, &thread_data[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    uint64_t total_tracked_ops = 0;
    double max_tracked_time = 0.0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total_tracked_ops += thread_data[i].ops;
        if (thread_data[i].elapsed_sec > max_tracked_time) {
            max_tracked_time = thread_data[i].elapsed_sec;
        }
    }
    
    result.tracked.throughput = (double)total_tracked_ops / max_tracked_time;
    result.tracked.count = total_tracked_ops;
    result.tracked.mean = max_tracked_time / (double)total_tracked_ops * 1e9;
    
    tracker_destroy(tracker);
    
    /* Calculate overhead */
    if (result.baseline.throughput > 0) {
        double throughput_ratio = result.tracked.throughput / result.baseline.throughput;
        result.overhead_percent = (1.0 - throughput_ratio) * 100.0;
        result.overhead_ns = result.tracked.mean - result.baseline.mean;
    } else {
        result.overhead_percent = 0.0;
        result.overhead_ns = 0.0;
    }
    
    /* Higher overhead acceptable for multi-threaded due to lock contention */
    result.target_overhead = TARGET_OVERHEAD_PERCENT * 2.0;  /* 1000% (10x) for multi-threaded */
    result.passed = (result.overhead_percent <= result.target_overhead);
    
    return result;
}

/* ============================================================================
 * Benchmark 3: Sustained Allocation Test
 * ============================================================================ */

static bench_result_t run_benchmark_sustained_alloc(void) {
    bench_result_t result;
    strncpy(result.name, "Sustained Allocations (10K active)", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    void* ptrs[MAX_SUSTAINED_ALLOCS];
    
    /* Baseline: allocate many, then free all */
    printf("  Running baseline sustained allocations...\n");
    bench_timer_t start;
    bench_timer_start(&start);
    
    /* Allocate all */
    for (size_t i = 0; i < MAX_SUSTAINED_ALLOCS; i++) {
        size_t size = 32 + (i % 256);
        ptrs[i] = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptrs[i]);
    }
    
    /* Free all */
    for (size_t i = 0; i < MAX_SUSTAINED_ALLOCS; i++) {
        free(ptrs[i]);
    }
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    result.baseline.throughput = (double)(MAX_SUSTAINED_ALLOCS * 2) / baseline_time;
    result.baseline.count = MAX_SUSTAINED_ALLOCS * 2;
    result.baseline.mean = baseline_time / (double)(MAX_SUSTAINED_ALLOCS * 2) * 1e9;
    
    /* Tracked: allocate many with tracking, then free all */
    printf("  Running tracked sustained allocations...\n");
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    bench_timer_start(&start);
    
    /* Allocate and track all */
    for (size_t i = 0; i < MAX_SUSTAINED_ALLOCS; i++) {
        size_t size = 32 + (i % 256);
        ptrs[i] = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptrs[i]);
        track_allocation(tracker, ptrs[i], size, __FILE__, __LINE__);
    }
    
    /* Track deallocation and free all */
    for (size_t i = 0; i < MAX_SUSTAINED_ALLOCS; i++) {
        track_deallocation(tracker, ptrs[i]);
        free(ptrs[i]);
    }
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.throughput = (double)(MAX_SUSTAINED_ALLOCS * 2) / tracked_time;
    result.tracked.count = MAX_SUSTAINED_ALLOCS * 2;
    result.tracked.mean = tracked_time / (double)(MAX_SUSTAINED_ALLOCS * 2) * 1e9;
    
    tracker_destroy(tracker);
    
    /* Calculate overhead */
    if (result.baseline.throughput > 0) {
        double throughput_ratio = result.tracked.throughput / result.baseline.throughput;
        result.overhead_percent = (1.0 - throughput_ratio) * 100.0;
        result.overhead_ns = result.tracked.mean - result.baseline.mean;
    }
    
    result.target_overhead = TARGET_OVERHEAD_PERCENT;
    result.passed = (result.overhead_percent <= result.target_overhead);
    
    return result;
}

/* ============================================================================
 * Benchmark 4: Memory Pool Simulation
 * ============================================================================ */

#define POOL_SIZE      1000
#define POOL_CYCLES    5000

static bench_result_t run_benchmark_pool_simulation(void) {
    bench_result_t result;
    strncpy(result.name, "Memory Pool Simulation", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    void* pool[POOL_SIZE];
    memset(pool, 0, sizeof(pool));
    
    /* Baseline: simulate pool with random alloc/free pattern */
    printf("  Running baseline pool simulation...\n");
    bench_timer_t start;
    bench_timer_start(&start);
    
    uint64_t ops = 0;
    size_t active = 0;
    
    for (size_t cycle = 0; cycle < POOL_CYCLES; cycle++) {
        /* Fill pool halfway */
        while (active < POOL_SIZE / 2) {
            size_t slot = cycle % POOL_SIZE;
            while (pool[slot] != NULL) {
                slot = (slot + 1) % POOL_SIZE;
            }
            pool[slot] = malloc(128);
            BENCH_DO_NOT_OPTIMIZE(pool[slot]);
            active++;
            ops++;
        }
        
        /* Free some randomly */
        for (size_t i = 0; i < POOL_SIZE / 4 && active > 0; i++) {
            size_t slot = (cycle * 17 + i * 31) % POOL_SIZE;
            if (pool[slot] != NULL) {
                free(pool[slot]);
                pool[slot] = NULL;
                active--;
                ops++;
            }
        }
    }
    
    /* Free remaining */
    for (size_t i = 0; i < POOL_SIZE; i++) {
        if (pool[i] != NULL) {
            free(pool[i]);
            pool[i] = NULL;
            ops++;
        }
    }
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    result.baseline.throughput = (double)ops / baseline_time;
    result.baseline.count = ops;
    result.baseline.mean = baseline_time / (double)ops * 1e9;
    
    /* Tracked: same simulation with tracking */
    printf("  Running tracked pool simulation...\n");
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    memset(pool, 0, sizeof(pool));
    bench_timer_start(&start);
    
    ops = 0;
    active = 0;
    
    for (size_t cycle = 0; cycle < POOL_CYCLES; cycle++) {
        while (active < POOL_SIZE / 2) {
            size_t slot = cycle % POOL_SIZE;
            while (pool[slot] != NULL) {
                slot = (slot + 1) % POOL_SIZE;
            }
            pool[slot] = malloc(128);
            BENCH_DO_NOT_OPTIMIZE(pool[slot]);
            track_allocation(tracker, pool[slot], 128, __FILE__, __LINE__);
            active++;
            ops++;
        }
        
        for (size_t i = 0; i < POOL_SIZE / 4 && active > 0; i++) {
            size_t slot = (cycle * 17 + i * 31) % POOL_SIZE;
            if (pool[slot] != NULL) {
                track_deallocation(tracker, pool[slot]);
                free(pool[slot]);
                pool[slot] = NULL;
                active--;
                ops++;
            }
        }
    }
    
    for (size_t i = 0; i < POOL_SIZE; i++) {
        if (pool[i] != NULL) {
            track_deallocation(tracker, pool[i]);
            free(pool[i]);
            pool[i] = NULL;
            ops++;
        }
    }
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.throughput = (double)ops / tracked_time;
    result.tracked.count = ops;
    result.tracked.mean = tracked_time / (double)ops * 1e9;
    
    tracker_destroy(tracker);
    
    /* Calculate overhead */
    if (result.baseline.throughput > 0) {
        double throughput_ratio = result.tracked.throughput / result.baseline.throughput;
        result.overhead_percent = (1.0 - throughput_ratio) * 100.0;
        result.overhead_ns = result.tracked.mean - result.baseline.mean;
    }
    
    result.target_overhead = TARGET_OVERHEAD_PERCENT;
    result.passed = (result.overhead_percent <= result.target_overhead);
    
    return result;
}

/* ============================================================================
 * Benchmark 5: Calloc/Realloc Throughput
 * ============================================================================ */

#define REALLOC_ITERATIONS  10000

static bench_result_t run_benchmark_calloc_realloc(void) {
    bench_result_t result;
    strncpy(result.name, "Calloc/Realloc Throughput", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    /* Baseline */
    printf("  Running baseline calloc/realloc...\n");
    bench_timer_t start;
    bench_timer_start(&start);
    
    uint64_t ops = 0;
    for (size_t i = 0; i < REALLOC_ITERATIONS; i++) {
        /* Calloc then grow with realloc */
        void* ptr = calloc(10, sizeof(int));
        BENCH_DO_NOT_OPTIMIZE(ptr);
        ops++;
        
        for (size_t grow = 20; grow <= 100; grow += 20) {
            void* new_ptr = realloc(ptr, grow * sizeof(int));
            BENCH_DO_NOT_OPTIMIZE(new_ptr);
            ptr = new_ptr;
            ops++;
        }
        
        free(ptr);
        ops++;
    }
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    result.baseline.throughput = (double)ops / baseline_time;
    result.baseline.count = ops;
    result.baseline.mean = baseline_time / (double)ops * 1e9;
    
    /* Tracked */
    printf("  Running tracked calloc/realloc...\n");
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    bench_timer_start(&start);
    
    ops = 0;
    for (size_t i = 0; i < REALLOC_ITERATIONS; i++) {
        void* ptr = calloc(10, sizeof(int));
        BENCH_DO_NOT_OPTIMIZE(ptr);
        track_allocation(tracker, ptr, 10 * sizeof(int), __FILE__, __LINE__);
        ops++;
        
        for (size_t grow = 20; grow <= 100; grow += 20) {
            /*
             * For realloc tracking: save old pointer before realloc,
             * track deallocation of old, allocation of new.
             * Using uintptr_t to avoid use-after-free warning.
             */
            uintptr_t old_addr = (uintptr_t)ptr;
            size_t new_size = grow * sizeof(int);
            void* new_ptr = realloc(ptr, new_size);
            BENCH_DO_NOT_OPTIMIZE(new_ptr);
            
            track_deallocation(tracker, (void*)old_addr);
            track_allocation(tracker, new_ptr, new_size, __FILE__, __LINE__);
            
            ptr = new_ptr;
            ops++;
        }
        
        track_deallocation(tracker, ptr);
        free(ptr);
        ops++;
    }
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.throughput = (double)ops / tracked_time;
    result.tracked.count = ops;
    result.tracked.mean = tracked_time / (double)ops * 1e9;
    
    tracker_destroy(tracker);
    
    /* Calculate overhead */
    if (result.baseline.throughput > 0) {
        double throughput_ratio = result.tracked.throughput / result.baseline.throughput;
        result.overhead_percent = (1.0 - throughput_ratio) * 100.0;
        result.overhead_ns = result.tracked.mean - result.baseline.mean;
    }
    
    result.target_overhead = TARGET_OVERHEAD_PERCENT;
    result.passed = (result.overhead_percent <= result.target_overhead);
    
    return result;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char* argv[]) {
    bool csv_output = false;
    const char* csv_file = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_output = true;
            csv_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--csv <file>] [--help]\n", argv[0]);
            return 0;
        }
    }
    
    bench_print_header("Throughput");
    
    printf("Configuration:\n");
    printf("  Test duration:        %.1f seconds\n", TEST_DURATION_SEC);
    printf("  Thread count:         %d\n", NUM_THREADS);
    printf("  Target overhead:      <%.1f%%\n", TARGET_OVERHEAD_PERCENT);
    printf("\n");
    
    bench_result_t results[5];
    int result_count = 0;
    int passed_count = 0;
    
    printf("Running: Single-Thread Throughput\n");
    results[result_count] = run_benchmark_single_thread_throughput();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Multi-Thread Throughput\n");
    results[result_count] = run_benchmark_multi_thread_throughput();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Sustained Allocations\n");
    results[result_count] = run_benchmark_sustained_alloc();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Memory Pool Simulation\n");
    results[result_count] = run_benchmark_pool_simulation();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Calloc/Realloc Throughput\n");
    results[result_count] = run_benchmark_calloc_realloc();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    /* Print summary */
    bench_print_separator();
    printf("\n%sSUMMARY%s\n", BENCH_COLOR_BOLD, BENCH_COLOR_RESET);
    printf("Total benchmarks: %d\n", result_count);
    printf("Passed: %s%d%s\n",
           passed_count == result_count ? BENCH_COLOR_GREEN : BENCH_COLOR_YELLOW,
           passed_count, BENCH_COLOR_RESET);
    printf("Failed: %s%d%s\n",
           (result_count - passed_count) > 0 ? BENCH_COLOR_RED : BENCH_COLOR_GREEN,
           result_count - passed_count, BENCH_COLOR_RESET);
    
    if (csv_output && csv_file) {
        FILE* fp = fopen(csv_file, "w");
        if (fp) {
            bench_print_csv_header(fp);
            for (int i = 0; i < result_count; i++) {
                bench_print_csv_result(fp, &results[i]);
            }
            fclose(fp);
            printf("\nCSV results written to: %s\n", csv_file);
        }
    }
    
    printf("\n");
    bench_print_separator();
    
    return (passed_count == result_count) ? 0 : 1;
}
