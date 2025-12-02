/**
 * @file test_simple_leaks.c
 * @brief Integration tests for simple memory leak scenarios
 * 
 * MEMRO-25: Integration Test Suite
 * 
 * Tests basic leak patterns that MemRogue should detect:
 * - Single allocation never freed
 * - Multiple allocations never freed
 * - Allocation freed partially
 * - Allocation freed then lost pointer
 */

#include "integration_common.h"
#include "memrogue_tracker.h"
#include "memrogue_leak_detector.h"

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test: Single small allocation leaked
 */
static int test_single_small_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Allocate and track a small block
    void* ptr = malloc(64);
    ASSERT_NOT_NULL(ptr, "malloc should succeed");
    
    bool tracked = track_allocation(tracker, ptr, 64, __FILE__, __LINE__);
    ASSERT_TRUE(tracked, "track_allocation should succeed");
    
    // Intentionally do NOT free - this is a leak
    
    // Verify the tracker detects it
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.active_allocations, "should have 1 active allocation");
    ASSERT_EQ(64, stats.active_bytes, "should have 64 active bytes");
    
    // Clean up tracker (not the leaked memory - that's the point)
    tracker_destroy(tracker);
    
    // Now free for real to prevent actual leak in test
    free(ptr);
    
    return TEST_PASS;
}

/**
 * Test: Multiple allocations of varying sizes leaked
 */
static int test_multiple_varied_leaks(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024};
    size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    void* ptrs[7];
    size_t total_bytes = 0;
    
    // Allocate all
    for (size_t i = 0; i < num_sizes; i++) {
        ptrs[i] = malloc(sizes[i]);
        ASSERT_NOT_NULL(ptrs[i], "malloc should succeed");
        
        bool tracked = track_allocation(tracker, ptrs[i], sizes[i], __FILE__, __LINE__);
        ASSERT_TRUE(tracked, "track_allocation should succeed");
        total_bytes += sizes[i];
    }
    
    // Don't free any - all are leaks
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(num_sizes, stats.active_allocations, "should have correct number of active allocations");
    ASSERT_EQ(total_bytes, stats.active_bytes, "should have correct active bytes");
    
    tracker_destroy(tracker);
    
    // Clean up for real
    for (size_t i = 0; i < num_sizes; i++) {
        free(ptrs[i]);
    }
    
    return TEST_PASS;
}

/**
 * Test: Partial cleanup (some allocations freed, some leaked)
 */
static int test_partial_cleanup(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* leak1 = malloc(100);
    void* freed1 = malloc(200);
    void* leak2 = malloc(300);
    void* freed2 = malloc(400);
    void* leak3 = malloc(500);
    
    ASSERT_NOT_NULL(leak1, "allocation should succeed");
    ASSERT_NOT_NULL(freed1, "allocation should succeed");
    ASSERT_NOT_NULL(leak2, "allocation should succeed");
    ASSERT_NOT_NULL(freed2, "allocation should succeed");
    ASSERT_NOT_NULL(leak3, "allocation should succeed");
    
    // Track all
    track_allocation(tracker, leak1, 100, __FILE__, __LINE__);
    track_allocation(tracker, freed1, 200, __FILE__, __LINE__);
    track_allocation(tracker, leak2, 300, __FILE__, __LINE__);
    track_allocation(tracker, freed2, 400, __FILE__, __LINE__);
    track_allocation(tracker, leak3, 500, __FILE__, __LINE__);
    
    // Free some
    track_deallocation(tracker, freed1);
    free(freed1);
    track_deallocation(tracker, freed2);
    free(freed2);
    
    // Verify leaks
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(5, stats.total_allocations, "should track 5 total allocations");
    ASSERT_EQ(2, stats.total_deallocations, "should track 2 deallocations");
    ASSERT_EQ(3, stats.active_allocations, "should have 3 leaks");
    ASSERT_EQ(900, stats.active_bytes, "should have 900 leaked bytes (100+300+500)");
    
    tracker_destroy(tracker);
    
    // Clean up leaks
    free(leak1);
    free(leak2);
    free(leak3);
    
    return TEST_PASS;
}

/**
 * Test: Calloc allocation leaked
 */
static int test_calloc_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Use calloc - common pattern
    int* array = calloc(100, sizeof(int));
    ASSERT_NOT_NULL(array, "calloc should succeed");
    
    size_t size = 100 * sizeof(int);
    track_allocation(tracker, array, size, __FILE__, __LINE__);
    
    // Verify zero-initialized
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(0, array[i], "calloc should zero-initialize");
    }
    
    // Don't free - leak
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.active_allocations, "should have 1 leak");
    ASSERT_EQ(size, stats.active_bytes, "should track calloc size");
    
    tracker_destroy(tracker);
    free(array);
    
    return TEST_PASS;
}

/**
 * Test: Realloc resulting in leak
 */
static int test_realloc_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    /* Initial allocation */
    void* ptr = malloc(100);
    ASSERT_NOT_NULL(ptr, "malloc should succeed");
    track_allocation(tracker, ptr, 100, __FILE__, __LINE__);
    
    /*
     * Save old pointer value for tracking before realloc.
     * We need to save this BEFORE calling realloc so we can
     * properly track the deallocation of the original block.
     * Note: We're only using the pointer value for tracking,
     * not dereferencing it - this is safe and intentional.
     */
    uintptr_t saved_addr = (uintptr_t)ptr;
    
    /* Realloc to larger - ptr may be invalidated after this */
    void* new_ptr = realloc(ptr, 500);
    ASSERT_NOT_NULL(new_ptr, "realloc should succeed");
    
    /* Track deallocation using saved address (cast back to void*) */
    track_deallocation(tracker, (void*)saved_addr);
    track_allocation(tracker, new_ptr, 500, __FILE__, __LINE__);
    
    // Don't free - leak the reallocated memory
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.active_allocations, "should have 1 active allocation after realloc");
    ASSERT_EQ(500, stats.active_bytes, "should track new realloc size");
    
    tracker_destroy(tracker);
    free(new_ptr);
    
    return TEST_PASS;
}

/**
 * Test: Large allocation leak
 */
static int test_large_allocation_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Allocate 1MB
    size_t size = 1024 * 1024;
    void* ptr = malloc(size);
    ASSERT_NOT_NULL(ptr, "large malloc should succeed");
    
    track_allocation(tracker, ptr, size, __FILE__, __LINE__);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.active_allocations, "should have 1 allocation");
    ASSERT_EQ(size, stats.active_bytes, "should track 1MB");
    
    tracker_destroy(tracker);
    free(ptr);
    
    return TEST_PASS;
}

/**
 * Test: Many small allocations leaked
 */
static int test_many_small_leaks(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const size_t count = 1000;
    const size_t size = 16;
    void* ptrs[1000];
    
    for (size_t i = 0; i < count; i++) {
        ptrs[i] = malloc(size);
        ASSERT_NOT_NULL(ptrs[i], "malloc should succeed");
        track_allocation(tracker, ptrs[i], size, __FILE__, __LINE__);
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(count, stats.active_allocations, "should track all allocations");
    ASSERT_EQ(count * size, stats.active_bytes, "should track total bytes");
    
    tracker_destroy(tracker);
    
    for (size_t i = 0; i < count; i++) {
        free(ptrs[i]);
    }
    
    return TEST_PASS;
}

/**
 * Test: Zero-size allocation (edge case)
 */
static int test_zero_size_allocation(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Zero-size malloc is implementation-defined
    void* ptr = malloc(0);
    
    if (ptr != NULL) {
        // Some implementations return a unique pointer
        track_allocation(tracker, ptr, 0, __FILE__, __LINE__);
        
        tracker_stats_t stats;
        tracker_get_stats(tracker, &stats);
        
        ASSERT_EQ(1, stats.active_allocations, "should track zero-size allocation");
        ASSERT_EQ(0, stats.active_bytes, "should have 0 bytes");
        
        free(ptr);
    }
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: String allocation leak (common pattern)
 */
static int test_string_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const char* original = "Hello, MemRogue! This is a test string that will be leaked.";
    size_t len = strlen(original) + 1;
    
    char* leaked_str = malloc(len);
    ASSERT_NOT_NULL(leaked_str, "string malloc should succeed");
    
    strcpy(leaked_str, original);
    track_allocation(tracker, leaked_str, len, __FILE__, __LINE__);
    
    // Verify content
    ASSERT_STR_EQ(original, leaked_str, "string content should match");
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.active_allocations, "should have 1 leak");
    ASSERT_EQ(len, stats.active_bytes, "should track string length");
    
    tracker_destroy(tracker);
    free(leaked_str);
    
    return TEST_PASS;
}

/**
 * Test: Allocation with immediate overwrite of pointer (lost reference)
 */
static int test_lost_reference(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(100);
    ASSERT_NOT_NULL(ptr, "malloc should succeed");
    track_allocation(tracker, ptr, 100, __FILE__, __LINE__);
    
    void* saved_ptr = ptr;  // Save for cleanup
    
    // Simulate losing the reference (common bug)
    ptr = malloc(200);  // Overwrites ptr, losing reference to first allocation
    ASSERT_NOT_NULL(ptr, "second malloc should succeed");
    track_allocation(tracker, ptr, 200, __FILE__, __LINE__);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(2, stats.active_allocations, "should have 2 allocations");
    ASSERT_EQ(300, stats.active_bytes, "should have 300 bytes (100 + 200)");
    
    tracker_destroy(tracker);
    free(saved_ptr);
    free(ptr);
    
    return TEST_PASS;
}

// ============================================================================
// Test Suite Definition
// ============================================================================

static test_case_t simple_leak_tests[] = {
    {"single_small_leak", "Single 64-byte allocation leaked", test_single_small_leak},
    {"multiple_varied_leaks", "Multiple allocations of varying sizes", test_multiple_varied_leaks},
    {"partial_cleanup", "Some allocations freed, some leaked", test_partial_cleanup},
    {"calloc_leak", "calloc allocation leaked", test_calloc_leak},
    {"realloc_leak", "realloc resulting in leak", test_realloc_leak},
    {"large_allocation_leak", "1MB allocation leaked", test_large_allocation_leak},
    {"many_small_leaks", "1000 small allocations leaked", test_many_small_leaks},
    {"zero_size_allocation", "Zero-size allocation edge case", test_zero_size_allocation},
    {"string_leak", "String duplication leaked", test_string_leak},
    {"lost_reference", "Pointer overwritten, losing reference", test_lost_reference},
};

int main(void) {
    test_suite_t suite = {
        .name = "Simple Leak Integration Tests",
        .tests = simple_leak_tests,
        .test_count = sizeof(simple_leak_tests) / sizeof(simple_leak_tests[0]),
    };
    
    int result = run_test_suite(&suite);
    
    return (result == TEST_PASS) ? 0 : 1;
}
