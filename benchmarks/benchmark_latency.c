/**
 * @file benchmark_latency.c
 * @brief Allocation/deallocation latency benchmarks for MemRogue
 * 
 * MEMRO-26: Performance Benchmarks
 * 
 * This benchmark measures the latency overhead of MemRogue tracking
 * for individual memory operations across various allocation sizes.
 * 
 * Benchmarks included:
 * 1. Small allocations (16-64 bytes) - typical struct allocations
 * 2. Medium allocations (256-1024 bytes) - buffer allocations
 * 3. Large allocations (4KB-64KB) - page-sized allocations
 * 4. Mixed size allocations - realistic workload mix
 * 5. Rapid alloc/free cycles - stress test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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

/** Number of warmup iterations */
#define WARMUP_ITERATIONS        1000

/** Number of measurement iterations */
#define MEASURE_ITERATIONS       50000

/* ============================================================================
 * Benchmark 1: Small Allocation Latency (16-64 bytes)
 * ============================================================================ */

/**
 * Benchmark small allocations without tracking (baseline).
 */
static void bench_small_alloc_baseline(bench_collector_t* collector) {
    for (size_t i = 0; i < MEASURE_ITERATIONS; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        /* Allocate small block */
        size_t size = 16 + (i % 49);  /* 16-64 bytes */
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        
        /* Immediately free */
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

/**
 * Benchmark small allocations with MemRogue tracking.
 */
static void bench_small_alloc_tracked(bench_collector_t* collector, 
                                       memory_tracker_t* tracker) {
    for (size_t i = 0; i < MEASURE_ITERATIONS; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        /* Allocate small block */
        size_t size = 16 + (i % 49);  /* 16-64 bytes */
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        
        /* Track allocation */
        track_allocation(tracker, ptr, size, __FILE__, __LINE__);
        
        /* Track deallocation and free */
        track_deallocation(tracker, ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

static bench_result_t run_benchmark_small_alloc(void) {
    bench_result_t result;
    strncpy(result.name, "Small Allocations (16-64 bytes)", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    bench_collector_t baseline_collector, tracked_collector;
    if (!bench_collector_init(&baseline_collector, MEASURE_ITERATIONS)) {
        fprintf(stderr, "Error: Failed to initialize baseline_collector\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Small Allocations (16-64 bytes)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    if (!bench_collector_init(&tracked_collector, MEASURE_ITERATIONS)) {
        fprintf(stderr, "Error: Failed to initialize tracked_collector\n");
        bench_collector_destroy(&baseline_collector);
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Small Allocations (16-64 bytes)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    
    /* Create tracker with backtraces disabled for fair comparison */
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;  /* Disable for latency measurement */
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    /* Warmup */
    printf("  Warming up...\n");
    for (size_t i = 0; i < WARMUP_ITERATIONS; i++) {
        void* ptr = malloc(32);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
    }
    
    /* Run baseline */
    printf("  Running baseline measurements...\n");
    bench_timer_t total_start;
    bench_timer_start(&total_start);
    bench_small_alloc_baseline(&baseline_collector);
    double baseline_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&baseline_collector, &result.baseline, baseline_time);
    
    /* Run tracked */
    printf("  Running tracked measurements...\n");
    bench_timer_start(&total_start);
    bench_small_alloc_tracked(&tracked_collector, tracker);
    double tracked_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&tracked_collector, &result.tracked, tracked_time);
    
    /* Calculate overhead */
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT);
    
    /* Cleanup */
    tracker_destroy(tracker);
    bench_collector_destroy(&baseline_collector);
    bench_collector_destroy(&tracked_collector);
    
    return result;
}

/* ============================================================================
 * Benchmark 2: Medium Allocation Latency (256-1024 bytes)
 * ============================================================================ */

static void bench_medium_alloc_baseline(bench_collector_t* collector) {
    for (size_t i = 0; i < MEASURE_ITERATIONS; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        size_t size = 256 + (i % 769);  /* 256-1024 bytes */
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

static void bench_medium_alloc_tracked(bench_collector_t* collector,
                                        memory_tracker_t* tracker) {
    for (size_t i = 0; i < MEASURE_ITERATIONS; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        size_t size = 256 + (i % 769);  /* 256-1024 bytes */
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        
        track_allocation(tracker, ptr, size, __FILE__, __LINE__);
        track_deallocation(tracker, ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

static bench_result_t run_benchmark_medium_alloc(void) {
    bench_result_t result;
    strncpy(result.name, "Medium Allocations (256-1024 bytes)", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    bench_collector_t baseline_collector, tracked_collector;
    if (!bench_collector_init(&baseline_collector, MEASURE_ITERATIONS)) {
        fprintf(stderr, "Error: Failed to initialize baseline_collector\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Medium Allocations (256-1024 bytes)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    if (!bench_collector_init(&tracked_collector, MEASURE_ITERATIONS)) {
        fprintf(stderr, "Error: Failed to initialize tracked_collector\n");
        bench_collector_destroy(&baseline_collector);
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Medium Allocations (256-1024 bytes)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    /* Warmup */
    printf("  Warming up...\n");
    for (size_t i = 0; i < WARMUP_ITERATIONS; i++) {
        void* ptr = malloc(512);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
    }
    
    /* Run baseline */
    printf("  Running baseline measurements...\n");
    bench_timer_t total_start;
    bench_timer_start(&total_start);
    bench_medium_alloc_baseline(&baseline_collector);
    double baseline_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&baseline_collector, &result.baseline, baseline_time);
    
    /* Run tracked */
    printf("  Running tracked measurements...\n");
    bench_timer_start(&total_start);
    bench_medium_alloc_tracked(&tracked_collector, tracker);
    double tracked_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&tracked_collector, &result.tracked, tracked_time);
    
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT);
    
    tracker_destroy(tracker);
    bench_collector_destroy(&baseline_collector);
    bench_collector_destroy(&tracked_collector);
    
    return result;
}

/* ============================================================================
 * Benchmark 3: Large Allocation Latency (4KB-64KB)
 * ============================================================================ */

static void bench_large_alloc_baseline(bench_collector_t* collector) {
    for (size_t i = 0; i < MEASURE_ITERATIONS / 10; i++) {  /* Fewer iterations for large allocs */
        bench_timer_t start;
        bench_timer_start(&start);
        
        size_t size = 4096 + (i % 61441);  /* 4KB-64KB */
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

static void bench_large_alloc_tracked(bench_collector_t* collector,
                                       memory_tracker_t* tracker) {
    for (size_t i = 0; i < MEASURE_ITERATIONS / 10; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        size_t size = 4096 + (i % 61441);  /* 4KB-64KB */
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        
        track_allocation(tracker, ptr, size, __FILE__, __LINE__);
        track_deallocation(tracker, ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

static bench_result_t run_benchmark_large_alloc(void) {
    bench_result_t result;
    strncpy(result.name, "Large Allocations (4KB-64KB)", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    bench_collector_t baseline_collector, tracked_collector;
    if (!bench_collector_init(&baseline_collector, MEASURE_ITERATIONS / 10)) {
        fprintf(stderr, "Error: Failed to initialize baseline_collector\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Large Allocations (4KB-64KB)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    if (!bench_collector_init(&tracked_collector, MEASURE_ITERATIONS / 10)) {
        fprintf(stderr, "Error: Failed to initialize tracked_collector\n");
        bench_collector_destroy(&baseline_collector);
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Large Allocations (4KB-64KB)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    printf("  Warming up...\n");
    for (size_t i = 0; i < WARMUP_ITERATIONS / 10; i++) {
        void* ptr = malloc(16384);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
    }
    
    printf("  Running baseline measurements...\n");
    bench_timer_t total_start;
    bench_timer_start(&total_start);
    bench_large_alloc_baseline(&baseline_collector);
    double baseline_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&baseline_collector, &result.baseline, baseline_time);
    
    printf("  Running tracked measurements...\n");
    bench_timer_start(&total_start);
    bench_large_alloc_tracked(&tracked_collector, tracker);
    double tracked_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&tracked_collector, &result.tracked, tracked_time);
    
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT);
    
    tracker_destroy(tracker);
    bench_collector_destroy(&baseline_collector);
    bench_collector_destroy(&tracked_collector);
    
    return result;
}

/* ============================================================================
 * Benchmark 4: Mixed Size Allocations (Realistic Workload)
 * ============================================================================ */

/* Allocation size distribution simulating realistic workload */
static const size_t MIXED_SIZES[] = {
    16, 24, 32, 48, 64,           /* Small structs (50% of allocations) */
    128, 256, 512,                 /* Medium buffers (30% of allocations) */
    1024, 2048, 4096, 8192        /* Large buffers (20% of allocations) */
};
static const size_t MIXED_SIZES_COUNT = sizeof(MIXED_SIZES) / sizeof(MIXED_SIZES[0]);

static void bench_mixed_alloc_baseline(bench_collector_t* collector) {
    for (size_t i = 0; i < MEASURE_ITERATIONS; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        /* Select size based on weighted distribution */
        size_t size_idx;
        size_t rand_val = i % 100;
        if (rand_val < 50) {
            size_idx = i % 5;        /* Small: 50% */
        } else if (rand_val < 80) {
            size_idx = 5 + (i % 3);  /* Medium: 30% */
        } else {
            size_idx = 8 + (i % 4);  /* Large: 20% */
        }
        size_t size = MIXED_SIZES[size_idx];
        
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

static void bench_mixed_alloc_tracked(bench_collector_t* collector,
                                       memory_tracker_t* tracker) {
    for (size_t i = 0; i < MEASURE_ITERATIONS; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        size_t size_idx;
        size_t rand_val = i % 100;
        if (rand_val < 50) {
            size_idx = i % 5;
        } else if (rand_val < 80) {
            size_idx = 5 + (i % 3);
        } else {
            size_idx = 8 + (i % 4);
        }
        size_t size = MIXED_SIZES[size_idx];
        
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        
        track_allocation(tracker, ptr, size, __FILE__, __LINE__);
        track_deallocation(tracker, ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

static bench_result_t run_benchmark_mixed_alloc(void) {
    bench_result_t result;
    strncpy(result.name, "Mixed Size Allocations (Realistic)", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    bench_collector_t baseline_collector, tracked_collector;
    if (!bench_collector_init(&baseline_collector, MEASURE_ITERATIONS)) {
        fprintf(stderr, "Error: Failed to initialize baseline_collector\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Mixed Size Allocations (Realistic)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    if (!bench_collector_init(&tracked_collector, MEASURE_ITERATIONS)) {
        fprintf(stderr, "Error: Failed to initialize tracked_collector\n");
        bench_collector_destroy(&baseline_collector);
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Mixed Size Allocations (Realistic)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    printf("  Warming up...\n");
    for (size_t i = 0; i < WARMUP_ITERATIONS; i++) {
        void* ptr = malloc(MIXED_SIZES[i % MIXED_SIZES_COUNT]);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
    }
    
    printf("  Running baseline measurements...\n");
    bench_timer_t total_start;
    bench_timer_start(&total_start);
    bench_mixed_alloc_baseline(&baseline_collector);
    double baseline_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&baseline_collector, &result.baseline, baseline_time);
    
    printf("  Running tracked measurements...\n");
    bench_timer_start(&total_start);
    bench_mixed_alloc_tracked(&tracked_collector, tracker);
    double tracked_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&tracked_collector, &result.tracked, tracked_time);
    
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT);
    
    tracker_destroy(tracker);
    bench_collector_destroy(&baseline_collector);
    bench_collector_destroy(&tracked_collector);
    
    return result;
}

/* ============================================================================
 * Benchmark 5: Rapid Alloc/Free Cycles (Stress Test)
 * ============================================================================ */

#define BATCH_SIZE 100

static void bench_rapid_cycle_baseline(bench_collector_t* collector) {
    void* ptrs[BATCH_SIZE];
    
    for (size_t i = 0; i < MEASURE_ITERATIONS / BATCH_SIZE; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        /* Allocate batch */
        for (size_t j = 0; j < BATCH_SIZE; j++) {
            ptrs[j] = malloc(64);
            BENCH_DO_NOT_OPTIMIZE(ptrs[j]);
        }
        
        /* Free batch in reverse order */
        for (size_t j = BATCH_SIZE; j > 0; j--) {
            free(ptrs[j - 1]);
        }
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        /* Record per-operation time */
        bench_collector_add(collector, (double)elapsed / (double)(BATCH_SIZE * 2));
    }
}

static void bench_rapid_cycle_tracked(bench_collector_t* collector,
                                       memory_tracker_t* tracker) {
    void* ptrs[BATCH_SIZE];
    
    for (size_t i = 0; i < MEASURE_ITERATIONS / BATCH_SIZE; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        /* Allocate and track batch */
        for (size_t j = 0; j < BATCH_SIZE; j++) {
            ptrs[j] = malloc(64);
            BENCH_DO_NOT_OPTIMIZE(ptrs[j]);
            track_allocation(tracker, ptrs[j], 64, __FILE__, __LINE__);
        }
        
        /* Track deallocation and free batch in reverse order */
        for (size_t j = BATCH_SIZE; j > 0; j--) {
            track_deallocation(tracker, ptrs[j - 1]);
            free(ptrs[j - 1]);
        }
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed / (double)(BATCH_SIZE * 2));
    }
}

static bench_result_t run_benchmark_rapid_cycle(void) {
    bench_result_t result;
    strncpy(result.name, "Rapid Alloc/Free Cycles (Batch)", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    bench_collector_t baseline_collector, tracked_collector;
    if (!bench_collector_init(&baseline_collector, MEASURE_ITERATIONS)) {
        fprintf(stderr, "Error: Failed to initialize baseline_collector\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Rapid Alloc/Free Cycles (Batch)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    if (!bench_collector_init(&tracked_collector, MEASURE_ITERATIONS)) {
        fprintf(stderr, "Error: Failed to initialize tracked_collector\n");
        bench_collector_destroy(&baseline_collector);
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Rapid Alloc/Free Cycles (Batch)", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    printf("  Warming up...\n");
    for (size_t i = 0; i < WARMUP_ITERATIONS / BATCH_SIZE; i++) {
        void* ptrs[BATCH_SIZE];
        for (size_t j = 0; j < BATCH_SIZE; j++) {
            ptrs[j] = malloc(64);
        }
        for (size_t j = 0; j < BATCH_SIZE; j++) {
            free(ptrs[j]);
        }
    }
    
    printf("  Running baseline measurements...\n");
    bench_timer_t total_start;
    bench_timer_start(&total_start);
    bench_rapid_cycle_baseline(&baseline_collector);
    double baseline_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&baseline_collector, &result.baseline, baseline_time);
    
    printf("  Running tracked measurements...\n");
    bench_timer_start(&total_start);
    bench_rapid_cycle_tracked(&tracked_collector, tracker);
    double tracked_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&tracked_collector, &result.tracked, tracked_time);
    
    /* Higher threshold for batch operations due to tight loop overhead accumulation */
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT * 3.0);
    
    tracker_destroy(tracker);
    bench_collector_destroy(&baseline_collector);
    bench_collector_destroy(&tracked_collector);
    
    return result;
}

/* ============================================================================
 * Benchmark 6: Allocation with Backtrace (Higher Overhead Expected)
 * ============================================================================ */

static void bench_backtrace_tracked(bench_collector_t* collector,
                                     memory_tracker_t* tracker) {
    for (size_t i = 0; i < MEASURE_ITERATIONS / 10; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        size_t size = 64;
        void* ptr = malloc(size);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        
        track_allocation(tracker, ptr, size, __FILE__, __LINE__);
        track_deallocation(tracker, ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(collector, (double)elapsed);
    }
}

static bench_result_t run_benchmark_with_backtrace(void) {
    bench_result_t result;
    strncpy(result.name, "Allocations with Backtrace Capture", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    size_t iterations = MEASURE_ITERATIONS / 10;
    bench_collector_t baseline_collector, tracked_collector;
    if (!bench_collector_init(&baseline_collector, iterations)) {
        fprintf(stderr, "Error: Failed to initialize baseline_collector\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Allocations with Backtrace Capture", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    if (!bench_collector_init(&tracked_collector, iterations)) {
        fprintf(stderr, "Error: Failed to initialize tracked_collector\n");
        bench_collector_destroy(&baseline_collector);
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Allocations with Backtrace Capture", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    
    /* Baseline without tracking */
    printf("  Running baseline measurements...\n");
    bench_timer_t total_start;
    bench_timer_start(&total_start);
    for (size_t i = 0; i < iterations; i++) {
        bench_timer_t start;
        bench_timer_start(&start);
        
        void* ptr = malloc(64);
        BENCH_DO_NOT_OPTIMIZE(ptr);
        free(ptr);
        
        uint64_t elapsed = bench_timer_elapsed_ns(&start);
        bench_collector_add(&baseline_collector, (double)elapsed);
    }
    double baseline_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&baseline_collector, &result.baseline, baseline_time);
    
    /* Tracked with backtraces enabled */
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = true;  /* Enable backtraces */
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    printf("  Running tracked measurements (with backtraces)...\n");
    bench_timer_start(&total_start);
    bench_backtrace_tracked(&tracked_collector, tracker);
    double tracked_time = bench_timer_elapsed_sec(&total_start);
    bench_compute_stats(&tracked_collector, &result.tracked, tracked_time);
    
    /* Higher overhead target for backtrace capture due to stack walking */
    bench_calculate_overhead(&result, 50000.0);
    
    tracker_destroy(tracker);
    bench_collector_destroy(&baseline_collector);
    bench_collector_destroy(&tracked_collector);
    
    return result;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char* argv[]) {
    bool csv_output = false;
    const char* csv_file = NULL;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_output = true;
            csv_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--csv <file>] [--help]\n", argv[0]);
            printf("  --csv <file>  Output results in CSV format to specified file\n");
            printf("  --help        Show this help message\n");
            return 0;
        }
    }
    
    bench_print_header("Latency");
    
    printf("Configuration:\n");
    printf("  Warmup iterations:    %d\n", WARMUP_ITERATIONS);
    printf("  Measurement iterations: %d\n", MEASURE_ITERATIONS);
    printf("  Target overhead:      <%.1f%%\n", TARGET_OVERHEAD_PERCENT);
    printf("\n");
    
    /* Store results for summary */
    bench_result_t results[7];
    int result_count = 0;
    int passed_count = 0;
    
    /* Run all benchmarks */
    printf("Running: Small Allocations (16-64 bytes)\n");
    results[result_count] = run_benchmark_small_alloc();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Medium Allocations (256-1024 bytes)\n");
    results[result_count] = run_benchmark_medium_alloc();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Large Allocations (4KB-64KB)\n");
    results[result_count] = run_benchmark_large_alloc();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Mixed Size Allocations\n");
    results[result_count] = run_benchmark_mixed_alloc();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Rapid Alloc/Free Cycles\n");
    results[result_count] = run_benchmark_rapid_cycle();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Allocations with Backtrace Capture\n");
    results[result_count] = run_benchmark_with_backtrace();
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
    
    /* Write CSV if requested */
    if (csv_output && csv_file) {
        FILE* fp = fopen(csv_file, "w");
        if (fp) {
            bench_print_csv_header(fp);
            for (int i = 0; i < result_count; i++) {
                bench_print_csv_result(fp, &results[i]);
            }
            fclose(fp);
            printf("\nCSV results written to: %s\n", csv_file);
        } else {
            fprintf(stderr, "Error: Could not open %s for writing\n", csv_file);
        }
    }
    
    printf("\n");
    bench_print_separator();
    
    /* Return non-zero if any benchmark failed */
    return (passed_count == result_count) ? 0 : 1;
}
