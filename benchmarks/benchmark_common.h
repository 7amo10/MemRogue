/**
 * @file benchmark_common.h
 * @brief Common utilities for MemRogue performance benchmarks
 * 
 * MEMRO-26: Performance Benchmarks
 * 
 * This header provides timing utilities, statistics computation,
 * and reporting infrastructure for all performance benchmarks.
 * 
 * Design Goals:
 * - High-precision timing using clock_gettime(CLOCK_MONOTONIC)
 * - Statistical analysis with mean, median, std dev, percentiles
 * - CSV and human-readable output formats
 * - Thread-safe where applicable
 */

#ifndef BENCHMARK_COMMON_H
#define BENCHMARK_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <float.h>

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/** Default number of warmup iterations before actual measurement */
#define BENCH_DEFAULT_WARMUP      1000

/** Default number of measurement iterations */
#define BENCH_DEFAULT_ITERATIONS  10000

/** Maximum benchmark name length */
#define BENCH_MAX_NAME_LEN        128

/** Maximum number of samples to store for percentile calculation */
#define BENCH_MAX_SAMPLES         100000

/* ============================================================================
 * Color Output (terminal support)
 * ============================================================================ */

#ifdef NO_COLOR
    #define BENCH_COLOR_RESET   ""
    #define BENCH_COLOR_RED     ""
    #define BENCH_COLOR_GREEN   ""
    #define BENCH_COLOR_YELLOW  ""
    #define BENCH_COLOR_BLUE    ""
    #define BENCH_COLOR_CYAN    ""
    #define BENCH_COLOR_BOLD    ""
#else
    #define BENCH_COLOR_RESET   "\033[0m"
    #define BENCH_COLOR_RED     "\033[0;31m"
    #define BENCH_COLOR_GREEN   "\033[0;32m"
    #define BENCH_COLOR_YELLOW  "\033[0;33m"
    #define BENCH_COLOR_BLUE    "\033[0;34m"
    #define BENCH_COLOR_CYAN    "\033[0;36m"
    #define BENCH_COLOR_BOLD    "\033[1m"
#endif

/* ============================================================================
 * High-Precision Timer
 * ============================================================================ */

/**
 * High-precision timestamp structure.
 * Uses CLOCK_MONOTONIC for accurate elapsed time measurement.
 */
typedef struct {
    struct timespec ts;
} bench_timer_t;

/**
 * Get current timestamp with nanosecond precision.
 * 
 * @param timer Output timer structure
 */
static inline void bench_timer_start(bench_timer_t* timer) {
    clock_gettime(CLOCK_MONOTONIC, &timer->ts);
}

/**
 * Calculate elapsed time in nanoseconds since timer start.
 * 
 * @param start Timer started at measurement begin
 * @return Elapsed time in nanoseconds
 */
static inline uint64_t bench_timer_elapsed_ns(const bench_timer_t* start) {
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    uint64_t start_ns = (uint64_t)start->ts.tv_sec * 1000000000ULL + (uint64_t)start->ts.tv_nsec;
    uint64_t end_ns = (uint64_t)end.tv_sec * 1000000000ULL + (uint64_t)end.tv_nsec;
    
    return end_ns - start_ns;
}

/**
 * Calculate elapsed time in microseconds.
 * 
 * @param start Timer started at measurement begin
 * @return Elapsed time in microseconds
 */
static inline double bench_timer_elapsed_us(const bench_timer_t* start) {
    return (double)bench_timer_elapsed_ns(start) / 1000.0;
}

/**
 * Calculate elapsed time in milliseconds.
 * 
 * @param start Timer started at measurement begin
 * @return Elapsed time in milliseconds
 */
static inline double bench_timer_elapsed_ms(const bench_timer_t* start) {
    return (double)bench_timer_elapsed_ns(start) / 1000000.0;
}

/**
 * Calculate elapsed time in seconds.
 * 
 * @param start Timer started at measurement begin
 * @return Elapsed time in seconds
 */
static inline double bench_timer_elapsed_sec(const bench_timer_t* start) {
    return (double)bench_timer_elapsed_ns(start) / 1000000000.0;
}

/* ============================================================================
 * Statistics Structure
 * ============================================================================ */

/**
 * Statistical results from benchmark measurements.
 */
typedef struct {
    uint64_t count;           /**< Number of samples */
    double min;               /**< Minimum value */
    double max;               /**< Maximum value */
    double mean;              /**< Arithmetic mean */
    double median;            /**< Median (50th percentile) */
    double std_dev;           /**< Standard deviation */
    double variance;          /**< Variance */
    double p50;               /**< 50th percentile (same as median) */
    double p90;               /**< 90th percentile */
    double p95;               /**< 95th percentile */
    double p99;               /**< 99th percentile */
    double sum;               /**< Sum of all values */
    double throughput;        /**< Operations per second (if applicable) */
} bench_stats_t;

/* ============================================================================
 * Sample Collection
 * ============================================================================ */

/**
 * Sample collector for statistical analysis.
 * Stores raw samples for accurate percentile calculation.
 */
typedef struct {
    double* samples;          /**< Array of sample values */
    size_t capacity;          /**< Allocated capacity */
    size_t count;             /**< Current number of samples */
    double running_sum;       /**< Running sum for online mean */
    double running_sum_sq;    /**< Running sum of squares for online variance */
    double min;               /**< Running minimum */
    double max;               /**< Running maximum */
} bench_collector_t;

/**
 * Initialize a sample collector.
 * 
 * @param collector The collector to initialize
 * @param capacity Maximum number of samples to store
 * @return true on success, false on allocation failure
 */
static inline bool bench_collector_init(bench_collector_t* collector, size_t capacity) {
    if (!collector) return false;
    
    if (capacity > BENCH_MAX_SAMPLES) {
        capacity = BENCH_MAX_SAMPLES;
    }
    
    collector->samples = (double*)malloc(capacity * sizeof(double));
    if (!collector->samples) return false;
    
    collector->capacity = capacity;
    collector->count = 0;
    collector->running_sum = 0.0;
    collector->running_sum_sq = 0.0;
    collector->min = DBL_MAX;
    collector->max = -DBL_MAX;
    
    return true;
}

/**
 * Destroy a sample collector and free resources.
 * 
 * @param collector The collector to destroy
 */
static inline void bench_collector_destroy(bench_collector_t* collector) {
    if (collector && collector->samples) {
        free(collector->samples);
        collector->samples = NULL;
        collector->capacity = 0;
        collector->count = 0;
    }
}

/**
 * Add a sample to the collector.
 * 
 * @param collector The collector
 * @param value The sample value to add
 * @return true if stored, false if capacity exceeded (still updates running stats)
 */
static inline bool bench_collector_add(bench_collector_t* collector, double value) {
    if (!collector) return false;
    
    /* Update running statistics regardless of storage */
    collector->running_sum += value;
    collector->running_sum_sq += value * value;
    
    if (value < collector->min) collector->min = value;
    if (value > collector->max) collector->max = value;
    
    /* Store sample if capacity allows */
    if (collector->count < collector->capacity) {
        collector->samples[collector->count++] = value;
        return true;
    }
    
    return false;
}

/**
 * Reset collector for reuse without reallocating.
 * 
 * @param collector The collector to reset
 */
static inline void bench_collector_reset(bench_collector_t* collector) {
    if (!collector) return;
    
    collector->count = 0;
    collector->running_sum = 0.0;
    collector->running_sum_sq = 0.0;
    collector->min = DBL_MAX;
    collector->max = -DBL_MAX;
}

/* ============================================================================
 * Comparison function for qsort
 * ============================================================================ */

/**
 * Compare two doubles for qsort.
 */
static int bench_compare_doubles(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* ============================================================================
 * Statistical Computation
 * ============================================================================ */

/**
 * Compute statistics from collected samples.
 * 
 * @param collector The sample collector
 * @param stats Output statistics structure
 * @param total_time_sec Total benchmark time in seconds (for throughput calculation)
 */
static inline void bench_compute_stats(bench_collector_t* collector, 
                                        bench_stats_t* stats,
                                        double total_time_sec) {
    if (!collector || !stats || collector->count == 0) {
        if (stats) memset(stats, 0, sizeof(bench_stats_t));
        return;
    }
    
    stats->count = collector->count;
    stats->sum = collector->running_sum;
    stats->min = collector->min;
    stats->max = collector->max;
    stats->mean = collector->running_sum / (double)collector->count;
    
    /* Calculate variance and standard deviation */
    double mean_sq = stats->mean * stats->mean;
    double avg_sq = collector->running_sum_sq / (double)collector->count;
    stats->variance = avg_sq - mean_sq;
    if (stats->variance < 0.0) stats->variance = 0.0; /* Handle floating point errors */
    stats->std_dev = sqrt(stats->variance);
    
    /* Calculate throughput */
    if (total_time_sec > 0.0) {
        stats->throughput = (double)collector->count / total_time_sec;
    } else {
        stats->throughput = 0.0;
    }
    
    /* Sort samples for percentile calculation */
    if (collector->count > 0) {
        qsort(collector->samples, collector->count, sizeof(double), bench_compare_doubles);
        
        /* Median (50th percentile) */
        size_t mid = collector->count / 2;
        if (collector->count % 2 == 0 && mid > 0) {
            stats->median = (collector->samples[mid - 1] + collector->samples[mid]) / 2.0;
        } else {
            stats->median = collector->samples[mid];
        }
        stats->p50 = stats->median;
        
        /* 90th percentile */
        size_t p90_idx = (size_t)((double)collector->count * 0.90);
        if (p90_idx >= collector->count) p90_idx = collector->count - 1;
        stats->p90 = collector->samples[p90_idx];
        
        /* 95th percentile */
        size_t p95_idx = (size_t)((double)collector->count * 0.95);
        if (p95_idx >= collector->count) p95_idx = collector->count - 1;
        stats->p95 = collector->samples[p95_idx];
        
        /* 99th percentile */
        size_t p99_idx = (size_t)((double)collector->count * 0.99);
        if (p99_idx >= collector->count) p99_idx = collector->count - 1;
        stats->p99 = collector->samples[p99_idx];
    }
}

/* ============================================================================
 * Benchmark Result Structure
 * ============================================================================ */

/**
 * Complete benchmark result including overhead comparison.
 */
typedef struct {
    char name[BENCH_MAX_NAME_LEN];     /**< Benchmark name */
    bench_stats_t baseline;            /**< Baseline stats (no tracking) */
    bench_stats_t tracked;             /**< Stats with tracking enabled */
    double overhead_percent;           /**< Percentage overhead */
    double overhead_ns;                /**< Absolute overhead in nanoseconds */
    bool passed;                       /**< Whether overhead is within target */
    double target_overhead;            /**< Target maximum overhead percentage */
} bench_result_t;

/**
 * Calculate overhead between baseline and tracked measurements.
 * 
 * @param result The benchmark result to update
 * @param target_overhead Maximum acceptable overhead percentage (e.g., 5.0 for 5%)
 */
static inline void bench_calculate_overhead(bench_result_t* result, double target_overhead) {
    if (!result) return;
    
    result->target_overhead = target_overhead;
    
    if (result->baseline.mean > 0.0) {
        result->overhead_ns = result->tracked.mean - result->baseline.mean;
        result->overhead_percent = (result->overhead_ns / result->baseline.mean) * 100.0;
    } else {
        result->overhead_ns = 0.0;
        result->overhead_percent = 0.0;
    }
    
    result->passed = (result->overhead_percent <= target_overhead);
}

/* ============================================================================
 * Output Formatting
 * ============================================================================ */

/**
 * Format a number with appropriate units (ns, µs, ms, s).
 * 
 * @param ns Time in nanoseconds
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
static inline void bench_format_time(double ns, char* buf, size_t buf_size) {
    if (ns < 1000.0) {
        snprintf(buf, buf_size, "%.2f ns", ns);
    } else if (ns < 1000000.0) {
        snprintf(buf, buf_size, "%.2f µs", ns / 1000.0);
    } else if (ns < 1000000000.0) {
        snprintf(buf, buf_size, "%.2f ms", ns / 1000000.0);
    } else {
        snprintf(buf, buf_size, "%.2f s", ns / 1000000000.0);
    }
}

/**
 * Format throughput with appropriate units.
 * 
 * @param ops_per_sec Operations per second
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
static inline void bench_format_throughput(double ops_per_sec, char* buf, size_t buf_size) {
    if (ops_per_sec >= 1000000000.0) {
        snprintf(buf, buf_size, "%.2f G ops/s", ops_per_sec / 1000000000.0);
    } else if (ops_per_sec >= 1000000.0) {
        snprintf(buf, buf_size, "%.2f M ops/s", ops_per_sec / 1000000.0);
    } else if (ops_per_sec >= 1000.0) {
        snprintf(buf, buf_size, "%.2f K ops/s", ops_per_sec / 1000.0);
    } else {
        snprintf(buf, buf_size, "%.2f ops/s", ops_per_sec);
    }
}

/**
 * Print a horizontal separator line.
 */
static inline void bench_print_separator(void) {
    printf("─────────────────────────────────────────────────────────────────────────────────\n");
}

/**
 * Print benchmark header.
 */
static inline void bench_print_header(const char* suite_name) {
    printf("\n");
    bench_print_separator();
    printf("%s%s MemRogue Performance Benchmark Suite %s%s\n", 
           BENCH_COLOR_BOLD, BENCH_COLOR_CYAN, suite_name, BENCH_COLOR_RESET);
    bench_print_separator();
    printf("\n");
}

/**
 * Print detailed statistics.
 * 
 * @param label Label for this measurement set
 * @param stats Statistics to print
 */
static inline void bench_print_stats(const char* label, const bench_stats_t* stats) {
    char time_buf[32];
    char tp_buf[32];
    
    printf("  %s%s:%s\n", BENCH_COLOR_BLUE, label, BENCH_COLOR_RESET);
    
    bench_format_time(stats->mean, time_buf, sizeof(time_buf));
    printf("    Mean:     %s\n", time_buf);
    
    bench_format_time(stats->median, time_buf, sizeof(time_buf));
    printf("    Median:   %s\n", time_buf);
    
    bench_format_time(stats->std_dev, time_buf, sizeof(time_buf));
    printf("    Std Dev:  %s\n", time_buf);
    
    bench_format_time(stats->min, time_buf, sizeof(time_buf));
    printf("    Min:      %s\n", time_buf);
    
    bench_format_time(stats->max, time_buf, sizeof(time_buf));
    printf("    Max:      %s\n", time_buf);
    
    bench_format_time(stats->p95, time_buf, sizeof(time_buf));
    printf("    P95:      %s\n", time_buf);
    
    bench_format_time(stats->p99, time_buf, sizeof(time_buf));
    printf("    P99:      %s\n", time_buf);
    
    if (stats->throughput > 0.0) {
        bench_format_throughput(stats->throughput, tp_buf, sizeof(tp_buf));
        printf("    Throughput: %s\n", tp_buf);
    }
}

/**
 * Print benchmark result with pass/fail status.
 * 
 * @param result The benchmark result to print
 */
static inline void bench_print_result(const bench_result_t* result) {
    char overhead_buf[32];
    
    printf("\n%s%s[%s]%s %s\n", 
           BENCH_COLOR_BOLD,
           result->passed ? BENCH_COLOR_GREEN : BENCH_COLOR_RED,
           result->passed ? "PASS" : "FAIL",
           BENCH_COLOR_RESET,
           result->name);
    
    bench_print_stats("Baseline (no tracking)", &result->baseline);
    bench_print_stats("With MemRogue tracking", &result->tracked);
    
    printf("\n  %sOverhead:%s\n", BENCH_COLOR_YELLOW, BENCH_COLOR_RESET);
    bench_format_time(result->overhead_ns, overhead_buf, sizeof(overhead_buf));
    printf("    Absolute: %s per operation\n", overhead_buf);
    printf("    Relative: %.2f%% (target: <%.1f%%)\n", 
           result->overhead_percent, result->target_overhead);
    
    printf("\n");
}

/**
 * Print CSV header for benchmark results.
 * 
 * @param fp File to write to (stdout or file)
 */
static inline void bench_print_csv_header(FILE* fp) {
    fprintf(fp, "benchmark,baseline_mean_ns,baseline_p95_ns,baseline_throughput,"
                "tracked_mean_ns,tracked_p95_ns,tracked_throughput,"
                "overhead_percent,overhead_ns,target_percent,passed\n");
}

/**
 * Print benchmark result as CSV row.
 * 
 * @param fp File to write to
 * @param result The benchmark result
 */
static inline void bench_print_csv_result(FILE* fp, const bench_result_t* result) {
    fprintf(fp, "%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%s\n",
            result->name,
            result->baseline.mean, result->baseline.p95, result->baseline.throughput,
            result->tracked.mean, result->tracked.p95, result->tracked.throughput,
            result->overhead_percent, result->overhead_ns,
            result->target_overhead,
            result->passed ? "true" : "false");
}

/* ============================================================================
 * Memory Barrier for Preventing Compiler Optimizations
 * ============================================================================ */

/**
 * Prevent compiler from optimizing away operations.
 * Use this to ensure allocations aren't eliminated.
 * 
 * Note: These macros use compiler-specific intrinsics:
 * - GCC/Clang: inline assembly
 * - MSVC: _ReadWriteBarrier (would need #include <intrin.h>)
 * - Other: volatile access as fallback
 */
#if defined(__GNUC__) || defined(__clang__)
    #define BENCH_DO_NOT_OPTIMIZE(x) do { \
        __asm__ __volatile__("" : : "r,m"(x) : "memory"); \
    } while (0)
    
    #define BENCH_MEMORY_BARRIER() do { \
        __asm__ __volatile__("" ::: "memory"); \
    } while (0)
#elif defined(_MSC_VER)
    #include <intrin.h>
    #define BENCH_DO_NOT_OPTIMIZE(x) do { \
        _ReadWriteBarrier(); \
        (void)(x); \
    } while (0)
    
    #define BENCH_MEMORY_BARRIER() _ReadWriteBarrier()
#else
    /* Fallback: use volatile to prevent optimization */
    #define BENCH_DO_NOT_OPTIMIZE(x) do { \
        volatile void* _bench_sink = (void*)&(x); \
        (void)_bench_sink; \
    } while (0)
    
    #define BENCH_MEMORY_BARRIER() do { \
        volatile int _bench_barrier = 0; \
        (void)_bench_barrier; \
    } while (0)
#endif

/* ============================================================================
 * Benchmark Macros
 * ============================================================================ */

/**
 * Run a warmup loop to stabilize caches and branch predictors.
 * 
 * @param warmup_count Number of warmup iterations
 * @param code Code block to execute
 */
#define BENCH_WARMUP(warmup_count, code) do { \
    for (size_t _warmup_i = 0; _warmup_i < (warmup_count); _warmup_i++) { \
        code; \
    } \
    BENCH_MEMORY_BARRIER(); \
} while (0)

/**
 * Run a timed benchmark collecting samples.
 * 
 * @param collector bench_collector_t* to store samples
 * @param iterations Number of iterations
 * @param code Code block to benchmark
 */
#define BENCH_TIMED(collector, iterations, code) do { \
    for (size_t _bench_i = 0; _bench_i < (iterations); _bench_i++) { \
        bench_timer_t _bench_start; \
        bench_timer_start(&_bench_start); \
        code; \
        uint64_t _bench_elapsed = bench_timer_elapsed_ns(&_bench_start); \
        bench_collector_add((collector), (double)_bench_elapsed); \
    } \
} while (0)

#endif /* BENCHMARK_COMMON_H */
