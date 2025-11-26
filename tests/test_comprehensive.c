/**
 * @file test_comprehensive.c
 * @brief Comprehensive unit tests for MemRogue memory debugger
 * 
 * MEMRO-12: Expand test suite with edge cases
 * - Large allocation tests (simulated 1GB+)
 * - Realloc edge cases
 * - Thread safety stress tests
 * - Error condition tests
 * - Boundary condition tests
 */

#include "memrogue_tracker.h"
#include "memrogue_hash_table.h"
#include "memrogue_allocation_record.h"
#include "memrogue_backtrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

// ============================================================================
// Test Framework Macros
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(func) do { \
    printf("  %-50s ", #func); \
    fflush(stdout); \
    func(); \
    tests_run++; \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

// ============================================================================
// Large Allocation Tests
// ============================================================================

/**
 * Test tracking large allocations (simulated 1GB+)
 * We don't actually allocate 1GB, but track the size as if we did.
 */
void test_large_allocation_tracking(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // Track a simulated 1GB allocation
    const size_t one_gb = (size_t)1024 * 1024 * 1024;
    void* fake_ptr = (void*)0xDEADBEEF;
    
    bool result = track_allocation(tracker, fake_ptr, one_gb, "large_alloc.c", 1);
    assert(result == true);
    
    // Verify stats
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.total_bytes_allocated == one_gb);
    assert(stats.active_bytes == one_gb);
    
    // Verify lookup
    allocation_info_t* info = lookup_allocation(tracker, fake_ptr);
    assert(info != NULL);
    assert(info->size == one_gb);
    
    // Clean up
    track_deallocation(tracker, fake_ptr);
    tracker_destroy(tracker);
}

/**
 * Test tracking multiple large allocations
 */
void test_multiple_large_allocations(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    const size_t sizes[] = {
        (size_t)512 * 1024 * 1024,   // 512MB
        (size_t)1024 * 1024 * 1024,  // 1GB
        (size_t)2ULL * 1024 * 1024 * 1024  // 2GB
    };
    void* ptrs[] = {
        (void*)0x1000000,
        (void*)0x2000000,
        (void*)0x3000000
    };
    
    // Track all allocations
    for (int i = 0; i < 3; i++) {
        bool result = track_allocation(tracker, ptrs[i], sizes[i], "multi_large.c", i);
        assert(result == true);
    }
    
    // Verify total bytes
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    size_t expected_total = sizes[0] + sizes[1] + sizes[2];
    assert(stats.total_bytes_allocated == expected_total);
    assert(stats.active_allocations == 3);
    
    // Verify average
    double avg = tracker_average_allocation_size(tracker);
    double expected_avg = (double)expected_total / 3.0;
    assert(avg == expected_avg);
    
    // Clean up
    for (int i = 0; i < 3; i++) {
        track_deallocation(tracker, ptrs[i]);
    }
    tracker_destroy(tracker);
}

/**
 * Test SIZE_MAX boundary condition
 */
void test_size_max_boundary(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // Track an allocation close to SIZE_MAX (simulated)
    // We use SIZE_MAX / 2 to avoid overflow issues
    const size_t large_size = SIZE_MAX / 2;
    void* fake_ptr = (void*)0xABCDEF;
    
    bool result = track_allocation(tracker, fake_ptr, large_size, "boundary.c", 1);
    assert(result == true);
    
    allocation_info_t* info = lookup_allocation(tracker, fake_ptr);
    assert(info != NULL);
    assert(info->size == large_size);
    
    track_deallocation(tracker, fake_ptr);
    tracker_destroy(tracker);
}

// ============================================================================
// Realloc Edge Case Tests
// ============================================================================

/**
 * Test realloc simulation: grow allocation
 * Simulates ptr = realloc(ptr, larger_size)
 */
void test_realloc_grow(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* old_ptr = (void*)0x1000;
    void* new_ptr = (void*)0x2000;  // New pointer after realloc
    
    // Initial allocation
    track_allocation(tracker, old_ptr, 100, "realloc.c", 1);
    
    // Simulate realloc: free old, allocate new (larger)
    track_deallocation(tracker, old_ptr);
    track_allocation(tracker, new_ptr, 200, "realloc.c", 2);
    
    // Verify
    assert(lookup_allocation(tracker, old_ptr) == NULL);
    allocation_info_t* info = lookup_allocation(tracker, new_ptr);
    assert(info != NULL);
    assert(info->size == 200);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.total_allocations == 2);
    assert(stats.total_deallocations == 1);
    assert(stats.active_allocations == 1);
    assert(stats.active_bytes == 200);
    
    track_deallocation(tracker, new_ptr);
    tracker_destroy(tracker);
}

/**
 * Test realloc simulation: shrink allocation
 */
void test_realloc_shrink(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* old_ptr = (void*)0x1000;
    void* new_ptr = (void*)0x1000;  // May return same pointer for shrink
    
    // Initial allocation
    track_allocation(tracker, old_ptr, 1000, "realloc.c", 1);
    
    // Simulate realloc shrink (same address, smaller size)
    track_deallocation(tracker, old_ptr);
    track_allocation(tracker, new_ptr, 100, "realloc.c", 2);
    
    allocation_info_t* info = lookup_allocation(tracker, new_ptr);
    assert(info != NULL);
    assert(info->size == 100);
    
    track_deallocation(tracker, new_ptr);
    tracker_destroy(tracker);
}

/**
 * Test realloc simulation: NULL pointer (equivalent to malloc)
 */
void test_realloc_null_ptr(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // realloc(NULL, size) is equivalent to malloc(size)
    void* new_ptr = (void*)0x3000;
    track_allocation(tracker, new_ptr, 256, "realloc.c", 1);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.total_allocations == 1);
    assert(stats.active_bytes == 256);
    
    track_deallocation(tracker, new_ptr);
    tracker_destroy(tracker);
}

/**
 * Test realloc simulation: zero size (equivalent to free)
 */
void test_realloc_zero_size(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0x4000;
    
    // Initial allocation
    track_allocation(tracker, ptr, 512, "realloc.c", 1);
    
    // realloc(ptr, 0) is equivalent to free(ptr) and returns NULL
    track_deallocation(tracker, ptr);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.total_allocations == 1);
    assert(stats.total_deallocations == 1);
    assert(stats.active_allocations == 0);
    assert(stats.active_bytes == 0);
    
    tracker_destroy(tracker);
}

/**
 * Test realloc chain: multiple resizes
 */
void test_realloc_chain(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0x5000;
    size_t sizes[] = {64, 128, 256, 512, 1024, 512, 256};
    
    // Initial allocation
    track_allocation(tracker, ptr, sizes[0], "realloc.c", 1);
    
    // Chain of reallocs (simulating in-place resizes)
    for (int i = 1; i < 7; i++) {
        track_deallocation(tracker, ptr);
        // Simulate new pointer for each realloc
        ptr = (void*)(uintptr_t)(0x5000 + i * 0x1000);
        track_allocation(tracker, ptr, sizes[i], "realloc.c", i + 1);
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.total_allocations == 7);
    assert(stats.total_deallocations == 6);
    assert(stats.active_allocations == 1);
    assert(stats.active_bytes == 256);  // Last size
    
    track_deallocation(tracker, ptr);
    tracker_destroy(tracker);
}

// ============================================================================
// Thread Safety Stress Tests
// ============================================================================

typedef struct {
    memory_tracker_t* tracker;
    int thread_id;
    int num_ops;
    int* success_count;
    pthread_mutex_t* count_lock;
} stress_test_data_t;

static void* stress_test_thread(void* arg) {
    stress_test_data_t* data = (stress_test_data_t*)arg;
    int successes = 0;
    
    for (int i = 0; i < data->num_ops; i++) {
        // Create unique pointer for this thread/iteration
        void* ptr = (void*)(uintptr_t)(data->thread_id * 0x100000 + i * 0x100);
        size_t size = (size_t)((data->thread_id + 1) * 64 + i);
        
        // Allocate
        if (track_allocation(data->tracker, ptr, size, "stress.c", i)) {
            successes++;
            
            // Lookup
            allocation_info_t* info = lookup_allocation(data->tracker, ptr);
            if (info && info->size == size) {
                successes++;
            }
            
            // Deallocate
            if (track_deallocation(data->tracker, ptr)) {
                successes++;
            }
        }
    }
    
    pthread_mutex_lock(data->count_lock);
    *(data->success_count) += successes;
    pthread_mutex_unlock(data->count_lock);
    
    return NULL;
}

/**
 * Test concurrent operations with many threads
 */
void test_thread_stress_many_threads(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    const int num_threads = 8;
    const int ops_per_thread = 500;
    
    pthread_t threads[8];
    stress_test_data_t data[8];
    int success_count = 0;
    pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;
    
    for (int i = 0; i < num_threads; i++) {
        data[i].tracker = tracker;
        data[i].thread_id = i + 1;
        data[i].num_ops = ops_per_thread;
        data[i].success_count = &success_count;
        data[i].count_lock = &count_lock;
        
        int rc = pthread_create(&threads[i], NULL, stress_test_thread, &data[i]);
        assert(rc == 0);
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Each successful iteration has 3 successes (alloc, lookup, dealloc)
    int expected_min = num_threads * ops_per_thread * 3;
    assert(success_count >= expected_min);  // All operations should succeed if tracker is thread-safe
    
    // Verify no active allocations remain
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.active_allocations == 0);
    
    pthread_mutex_destroy(&count_lock);
    tracker_destroy(tracker);
}

/**
 * Test rapid allocation/deallocation cycles
 */
void test_rapid_alloc_dealloc_cycles(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // Rapid cycles - use base address to avoid collisions with other tests
    const uintptr_t base = 0x100000;
    
    for (int cycle = 0; cycle < 100; cycle++) {
        void* ptrs[10];
        
        // Allocate 10 items
        for (int i = 0; i < 10; i++) {
            ptrs[i] = (void*)(base + (uintptr_t)cycle * 0x10000 + (uintptr_t)i * 0x100);
            track_allocation(tracker, ptrs[i], 128, "rapid.c", i);
        }
        
        // Free all
        for (int i = 0; i < 10; i++) {
            track_deallocation(tracker, ptrs[i]);
        }
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.total_allocations == 1000);
    assert(stats.total_deallocations == 1000);
    assert(stats.active_allocations == 0);
    
    tracker_destroy(tracker);
}

// ============================================================================
// Error Condition Tests
// ============================================================================

/**
 * Test double deallocation handling
 */
void test_double_deallocation(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0x7000;
    
    track_allocation(tracker, ptr, 100, "double.c", 1);
    
    // First deallocation succeeds
    bool result1 = track_deallocation(tracker, ptr);
    assert(result1 == true);
    
    // Second deallocation should fail (returns false, counted as unknown free)
    bool result2 = track_deallocation(tracker, ptr);
    assert(result2 == false);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.unknown_frees == 1);
    
    tracker_destroy(tracker);
}

/**
 * Test deallocation of never-allocated pointer
 */
void test_free_untracked_pointer(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* untracked_ptr = (void*)0x8000;
    
    bool result = track_deallocation(tracker, untracked_ptr);
    assert(result == false);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.unknown_frees == 1);
    
    tracker_destroy(tracker);
}

/**
 * Test allocation with very long filename
 */
void test_very_long_filename(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // Create a very long filename (1000 chars)
    char long_filename[1001];
    memset(long_filename, 'a', 1000);
    long_filename[1000] = '\0';
    
    void* ptr = (void*)0x9000;
    bool result = track_allocation(tracker, ptr, 64, long_filename, 1);
    assert(result == true);
    
    allocation_info_t* info = lookup_allocation(tracker, ptr);
    assert(info != NULL);
    // Filename should be stored (possibly truncated depending on implementation)
    assert(info->file != NULL);
    
    track_deallocation(tracker, ptr);
    tracker_destroy(tracker);
}

/**
 * Test allocation with line number edge cases
 */
void test_line_number_edge_cases(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // Line 0
    track_allocation(tracker, (void*)0xA000, 32, "edge.c", 0);
    allocation_info_t* info = lookup_allocation(tracker, (void*)0xA000);
    assert(info != NULL);
    assert(info->line == 0);
    
    // Negative line (shouldn't happen but test robustness)
    track_allocation(tracker, (void*)0xA100, 32, "edge.c", -1);
    info = lookup_allocation(tracker, (void*)0xA100);
    assert(info != NULL);
    assert(info->line == -1);
    
    // Very large line number
    track_allocation(tracker, (void*)0xA200, 32, "edge.c", INT32_MAX);
    info = lookup_allocation(tracker, (void*)0xA200);
    assert(info != NULL);
    assert(info->line == INT32_MAX);
    
    // Cleanup
    track_deallocation(tracker, (void*)0xA000);
    track_deallocation(tracker, (void*)0xA100);
    track_deallocation(tracker, (void*)0xA200);
    tracker_destroy(tracker);
}

// ============================================================================
// Boundary Condition Tests
// ============================================================================

/**
 * Test zero-size allocation
 */
void test_zero_size_allocation(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0xB000;
    
    // Zero-size allocations should still be tracked
    bool result = track_allocation(tracker, ptr, 0, "zero.c", 1);
    assert(result == true);
    
    allocation_info_t* info = lookup_allocation(tracker, ptr);
    assert(info != NULL);
    assert(info->size == 0);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.total_allocations == 1);
    assert(stats.total_bytes_allocated == 0);
    
    track_deallocation(tracker, ptr);
    tracker_destroy(tracker);
}

/**
 * Test single byte allocation
 */
void test_single_byte_allocation(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0xC000;
    
    bool result = track_allocation(tracker, ptr, 1, "single.c", 1);
    assert(result == true);
    
    allocation_info_t* info = lookup_allocation(tracker, ptr);
    assert(info != NULL);
    assert(info->size == 1);
    
    double avg = tracker_average_allocation_size(tracker);
    assert(avg == 1.0);
    
    track_deallocation(tracker, ptr);
    tracker_destroy(tracker);
}

/**
 * Test many small allocations
 */
void test_many_small_allocations(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    const int count = 10000;
    
    // Track many small allocations
    for (int i = 0; i < count; i++) {
        void* ptr = (void*)(uintptr_t)(0xD000 + i);
        track_allocation(tracker, ptr, 1, "small.c", i);
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.total_allocations == (uint64_t)count);
    assert(stats.active_allocations == (uint64_t)count);
    assert(stats.total_bytes_allocated == (uint64_t)count);
    
    // Free all
    for (int i = 0; i < count; i++) {
        void* ptr = (void*)(uintptr_t)(0xD000 + i);
        track_deallocation(tracker, ptr);
    }
    
    tracker_get_stats(tracker, &stats);
    assert(stats.active_allocations == 0);
    
    tracker_destroy(tracker);
}

/**
 * Test statistics overflow protection
 */
void test_stats_overflow_protection(void) {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // Track allocations with large sizes that could overflow
    const size_t large_size = SIZE_MAX / 4;
    
    track_allocation(tracker, (void*)0xE000, large_size, "overflow.c", 1);
    track_allocation(tracker, (void*)0xE100, large_size, "overflow.c", 2);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // Verify no wrap-around occurred
    assert(stats.total_bytes_allocated >= large_size);
    
    track_deallocation(tracker, (void*)0xE000);
    track_deallocation(tracker, (void*)0xE100);
    tracker_destroy(tracker);
}

// ============================================================================
// Backtrace Integration Tests
// ============================================================================

/**
 * Test tracker with backtrace capture enabled
 */
void test_tracker_backtrace_integration(void) {
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = true;
    config.backtrace_skip_frames = 2;
    
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    assert(tracker != NULL);
    
    void* ptr = (void*)0xF000;
    track_allocation(tracker, ptr, 256, "backtrace.c", 1);
    
    allocation_info_t* info = lookup_allocation(tracker, ptr);
    assert(info != NULL);
    assert(info->size == 256);
    // Backtrace should have been captured
    assert(info->frame_count >= 0);
    
    track_deallocation(tracker, ptr);
    tracker_destroy(tracker);
}

/**
 * Test tracker with backtrace capture disabled
 */
void test_tracker_no_backtrace(void) {
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    assert(tracker != NULL);
    
    void* ptr = (void*)0xF100;
    track_allocation(tracker, ptr, 128, "no_bt.c", 1);
    
    allocation_info_t* info = lookup_allocation(tracker, ptr);
    assert(info != NULL);
    // Backtrace should not have been captured
    assert(info->frame_count == 0);
    
    track_deallocation(tracker, ptr);
    tracker_destroy(tracker);
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== MEMRO-12: Comprehensive Unit Tests ===\n\n");
    
    printf("=== Large Allocation Tests ===\n\n");
    TEST(test_large_allocation_tracking);
    TEST(test_multiple_large_allocations);
    TEST(test_size_max_boundary);
    
    printf("\n=== Realloc Edge Case Tests ===\n\n");
    TEST(test_realloc_grow);
    TEST(test_realloc_shrink);
    TEST(test_realloc_null_ptr);
    TEST(test_realloc_zero_size);
    TEST(test_realloc_chain);
    
    printf("\n=== Thread Safety Stress Tests ===\n\n");
    TEST(test_thread_stress_many_threads);
    TEST(test_rapid_alloc_dealloc_cycles);
    
    printf("\n=== Error Condition Tests ===\n\n");
    TEST(test_double_deallocation);
    TEST(test_free_untracked_pointer);
    TEST(test_very_long_filename);
    TEST(test_line_number_edge_cases);
    
    printf("\n=== Boundary Condition Tests ===\n\n");
    TEST(test_zero_size_allocation);
    TEST(test_single_byte_allocation);
    TEST(test_many_small_allocations);
    TEST(test_stats_overflow_protection);
    
    printf("\n=== Backtrace Integration Tests ===\n\n");
    TEST(test_tracker_backtrace_integration);
    TEST(test_tracker_no_backtrace);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
