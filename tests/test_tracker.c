#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "memrogue_tracker.h"

// Test framework macros
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    fflush(stdout); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

// ============================================================================
// Configuration Tests
// ============================================================================

void test_tracker_config_init() {
    tracker_config_t config;
    tracker_config_init(&config);
    
    assert(config.hash_table_size == 4096);
    assert(config.capture_backtraces == true);
    assert(config.backtrace_skip_frames == 2);
    assert(config.use_frame_filter == true);
    assert(config.frame_filter.pattern_count > 0);
    
    // NULL handling
    tracker_config_init(NULL);  // Should not crash
}

// ============================================================================
// Tracker Lifecycle Tests
// ============================================================================

void test_tracker_create() {
    memory_tracker_t* tracker = tracker_create();
    
    assert(tracker != NULL);
    assert(tracker->initialized == true);
    assert(tracker->allocations != NULL);
    
    tracker_destroy(tracker);
}

void test_tracker_create_with_config() {
    tracker_config_t config;
    tracker_config_init(&config);
    config.hash_table_size = 1024;
    config.capture_backtraces = false;
    
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    assert(tracker != NULL);
    assert(tracker->initialized == true);
    assert(tracker->config.hash_table_size == 1024);
    assert(tracker->config.capture_backtraces == false);
    
    tracker_destroy(tracker);
}

void test_tracker_destroy_null() {
    // Should not crash
    tracker_destroy(NULL);
}

// ============================================================================
// Track Allocation Tests
// ============================================================================

void test_track_allocation_basic() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0x1000;
    bool result = track_allocation(tracker, ptr, 100, "test.c", 10);
    
    assert(result == true);
    assert(tracker_active_count(tracker) == 1);
    assert(tracker_active_bytes(tracker) == 100);
    
    tracker_destroy(tracker);
}

void test_track_allocation_null_ptr() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    bool result = track_allocation(tracker, NULL, 100, "test.c", 10);
    
    assert(result == false);
    assert(tracker_active_count(tracker) == 0);
    
    tracker_destroy(tracker);
}

void test_track_allocation_null_tracker() {
    bool result = track_allocation(NULL, (void*)0x1000, 100, "test.c", 10);
    assert(result == false);
}

void test_track_allocation_multiple() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    assert(track_allocation(tracker, (void*)0x1000, 100, "test.c", 1) == true);
    assert(track_allocation(tracker, (void*)0x2000, 200, "test.c", 2) == true);
    assert(track_allocation(tracker, (void*)0x3000, 300, "test.c", 3) == true);
    
    assert(tracker_active_count(tracker) == 3);
    assert(tracker_active_bytes(tracker) == 600);
    
    tracker_destroy(tracker);
}

void test_track_allocation_with_backtrace() {
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = true;
    
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    assert(tracker != NULL);
    
    // Use real pointer from malloc to get valid backtrace
    void* ptr = malloc(100);
    bool result = track_allocation(tracker, ptr, 100, "test.c", 10);
    
    assert(result == true);
    
    allocation_info_t* info = lookup_allocation(tracker, ptr);
    assert(info != NULL);
    
    // Backtrace should have been captured (if available)
    printf("\n    Captured %d frames\n", info->frame_count);
    
    // Clean up: track deallocation before freeing
    track_deallocation(tracker, ptr);
    free(ptr);
    tracker_destroy(tracker);
}

// ============================================================================
// Track Deallocation Tests
// ============================================================================

void test_track_deallocation_basic() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0x1000;
    track_allocation(tracker, ptr, 100, "test.c", 10);
    
    assert(tracker_active_count(tracker) == 1);
    
    bool result = track_deallocation(tracker, ptr);
    
    assert(result == true);
    assert(tracker_active_count(tracker) == 0);
    assert(tracker_active_bytes(tracker) == 0);
    
    tracker_destroy(tracker);
}

void test_track_deallocation_unknown() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // Try to deallocate a pointer that was never tracked
    bool result = track_deallocation(tracker, (void*)0x9999);
    
    assert(result == false);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    assert(stats.unknown_frees == 1);
    
    tracker_destroy(tracker);
}

void test_track_deallocation_null() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    bool result = track_deallocation(tracker, NULL);
    assert(result == false);
    
    tracker_destroy(tracker);
}

// ============================================================================
// Lookup Tests
// ============================================================================

void test_lookup_allocation_found() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0x1000;
    track_allocation(tracker, ptr, 100, "test.c", 10);
    
    allocation_info_t* info = lookup_allocation(tracker, ptr);
    
    assert(info != NULL);
    assert(info->ptr == ptr);
    assert(info->size == 100);
    assert(info->line == 10);
    assert(strcmp(info->file, "test.c") == 0);
    
    tracker_destroy(tracker);
}

void test_lookup_allocation_not_found() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    allocation_info_t* info = lookup_allocation(tracker, (void*)0x9999);
    
    assert(info == NULL);
    
    tracker_destroy(tracker);
}

void test_lookup_allocation_after_free() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    void* ptr = (void*)0x1000;
    track_allocation(tracker, ptr, 100, "test.c", 10);
    track_deallocation(tracker, ptr);
    
    allocation_info_t* info = lookup_allocation(tracker, ptr);
    
    assert(info == NULL);
    
    tracker_destroy(tracker);
}

// ============================================================================
// Statistics Tests
// ============================================================================

void test_tracker_stats_basic() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    track_allocation(tracker, (void*)0x1000, 100, "test.c", 1);
    track_allocation(tracker, (void*)0x2000, 200, "test.c", 2);
    track_deallocation(tracker, (void*)0x1000);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    assert(stats.total_allocations == 2);
    assert(stats.total_deallocations == 1);
    assert(stats.active_allocations == 1);
    assert(stats.peak_allocations == 2);
    
    assert(stats.total_bytes_allocated == 300);
    assert(stats.total_bytes_freed == 100);
    assert(stats.active_bytes == 200);
    assert(stats.peak_bytes == 300);
    
    tracker_destroy(tracker);
}

void test_tracker_stats_peak() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    // Allocate 5 items
    for (int i = 0; i < 5; i++) {
        track_allocation(tracker, (void*)(uintptr_t)(0x1000 + i * 0x100), 100, "test.c", i);
    }
    
    // Free 3 of them
    track_deallocation(tracker, (void*)0x1000);
    track_deallocation(tracker, (void*)0x1100);
    track_deallocation(tracker, (void*)0x1200);
    
    // Allocate 2 more
    track_allocation(tracker, (void*)0x5000, 100, "test.c", 10);
    track_allocation(tracker, (void*)0x6000, 100, "test.c", 11);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // Peak should still be 5 (from initial allocations)
    assert(stats.peak_allocations == 5);
    assert(stats.active_allocations == 4);
    
    tracker_destroy(tracker);
}

void test_tracker_reset_stats() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    track_allocation(tracker, (void*)0x1000, 100, "test.c", 1);
    track_allocation(tracker, (void*)0x2000, 200, "test.c", 2);
    
    tracker_reset_stats(tracker);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // Cumulative counters should be reset
    assert(stats.total_allocations == 0);
    assert(stats.total_deallocations == 0);
    
    // Active counts should remain
    assert(stats.active_allocations == 2);
    assert(stats.active_bytes == 300);
    
    tracker_destroy(tracker);
}

// ============================================================================
// Iteration Tests
// ============================================================================

static int iteration_count = 0;
static size_t iteration_total_size = 0;

static bool count_allocations(const allocation_info_t* info, void* user_data) {
    (void)user_data;
    iteration_count++;
    iteration_total_size += info->size;
    return true;  // Continue iteration
}

static bool stop_after_two(const allocation_info_t* info, void* user_data) {
    (void)info;
    int* count = (int*)user_data;
    (*count)++;
    return *count < 2;  // Stop after 2
}

void test_tracker_iterate() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    track_allocation(tracker, (void*)0x1000, 100, "test.c", 1);
    track_allocation(tracker, (void*)0x2000, 200, "test.c", 2);
    track_allocation(tracker, (void*)0x3000, 300, "test.c", 3);
    
    iteration_count = 0;
    iteration_total_size = 0;
    
    tracker_iterate(tracker, count_allocations, NULL);
    
    assert(iteration_count == 3);
    assert(iteration_total_size == 600);
    
    tracker_destroy(tracker);
}

void test_tracker_iterate_early_stop() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    track_allocation(tracker, (void*)0x1000, 100, "test.c", 1);
    track_allocation(tracker, (void*)0x2000, 200, "test.c", 2);
    track_allocation(tracker, (void*)0x3000, 300, "test.c", 3);
    
    int count = 0;
    tracker_iterate(tracker, stop_after_two, &count);
    
    assert(count == 2);
    
    tracker_destroy(tracker);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

typedef struct {
    memory_tracker_t* tracker;
    int thread_id;
    int num_ops;
} thread_test_data_t;

static void* thread_test_func(void* arg) {
    thread_test_data_t* data = (thread_test_data_t*)arg;
    
    for (int i = 0; i < data->num_ops; i++) {
        void* ptr = (void*)(uintptr_t)(data->thread_id * 0x10000 + i * 0x100);
        track_allocation(data->tracker, ptr, 64, "thread_test.c", i);
    }
    
    // Small delay to ensure overlap
    for (volatile int j = 0; j < 1000; j++);
    
    for (int i = 0; i < data->num_ops; i++) {
        void* ptr = (void*)(uintptr_t)(data->thread_id * 0x10000 + i * 0x100);
        track_deallocation(data->tracker, ptr);
    }
    
    return NULL;
}

void test_tracker_multithreaded() {
    memory_tracker_t* tracker = tracker_create();
    assert(tracker != NULL);
    
    const int num_threads = 4;
    const int ops_per_thread = 100;
    
    pthread_t threads[4];
    thread_test_data_t thread_data[4];
    
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].tracker = tracker;
        thread_data[i].thread_id = i + 1;
        thread_data[i].num_ops = ops_per_thread;
        
        int rc = pthread_create(&threads[i], NULL, thread_test_func, &thread_data[i]);
        assert(rc == 0);  // Fail test if thread creation fails
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // All allocations should be freed
    assert(tracker_active_count(tracker) == 0);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // Total operations should match
    assert(stats.total_allocations == (uint64_t)(num_threads * ops_per_thread));
    assert(stats.total_deallocations == (uint64_t)(num_threads * ops_per_thread));
    
    printf("\n    Multithreaded: %d threads x %d ops = %lu total allocs\n",
           num_threads, ops_per_thread, (unsigned long)stats.total_allocations);
    
    tracker_destroy(tracker);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

void test_tracker_no_leaks() {
    // Create and destroy tracker multiple times to check for leaks
    for (int i = 0; i < 100; i++) {
        memory_tracker_t* tracker = tracker_create();
        assert(tracker != NULL);
        
        for (int j = 0; j < 10; j++) {
            void* ptr = (void*)(uintptr_t)(0x1000 + j * 0x100);
            track_allocation(tracker, ptr, 64, "leak_test.c", j);
        }
        
        // Don't dealloc - tracker should clean up
        tracker_destroy(tracker);
    }
    // If we get here without crashing or leaking, test passes
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Tracker Configuration Tests ===\n\n");
    
    TEST(test_tracker_config_init);
    
    printf("\n=== Tracker Lifecycle Tests ===\n\n");
    
    TEST(test_tracker_create);
    TEST(test_tracker_create_with_config);
    TEST(test_tracker_destroy_null);
    
    printf("\n=== Track Allocation Tests ===\n\n");
    
    TEST(test_track_allocation_basic);
    TEST(test_track_allocation_null_ptr);
    TEST(test_track_allocation_null_tracker);
    TEST(test_track_allocation_multiple);
    TEST(test_track_allocation_with_backtrace);
    
    printf("\n=== Track Deallocation Tests ===\n\n");
    
    TEST(test_track_deallocation_basic);
    TEST(test_track_deallocation_unknown);
    TEST(test_track_deallocation_null);
    
    printf("\n=== Lookup Tests ===\n\n");
    
    TEST(test_lookup_allocation_found);
    TEST(test_lookup_allocation_not_found);
    TEST(test_lookup_allocation_after_free);
    
    printf("\n=== Statistics Tests ===\n\n");
    
    TEST(test_tracker_stats_basic);
    TEST(test_tracker_stats_peak);
    TEST(test_tracker_reset_stats);
    
    printf("\n=== Iteration Tests ===\n\n");
    
    TEST(test_tracker_iterate);
    TEST(test_tracker_iterate_early_stop);
    
    printf("\n=== Thread Safety Tests ===\n\n");
    
    TEST(test_tracker_multithreaded);
    
    printf("\n=== Edge Case Tests ===\n\n");
    
    TEST(test_tracker_no_leaks);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
