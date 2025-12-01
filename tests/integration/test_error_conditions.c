/**
 * @file test_error_conditions.c
 * @brief Integration tests for error conditions and edge cases
 * 
 * MEMRO-25: Integration Test Suite
 * 
 * Tests error handling and edge cases:
 * - Double-free detection
 * - Invalid-free detection
 * - NULL pointer handling
 * - Tracker NULL handling
 * - Allocation failures
 * - Edge case sizes
 */

#include "integration_common.h"
#include "memrogue_tracker.h"
#include "memrogue_double_free.h"
#include "memrogue_invalid_free.h"

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test: Double-free detection
 */
static int test_double_free_detection(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(100);
    ASSERT_NOT_NULL(ptr, "malloc should succeed");
    
    track_allocation(tracker, ptr, 100, __FILE__, __LINE__);
    
    // First free - should succeed
    bool result = track_deallocation(tracker, ptr);
    ASSERT_TRUE(result, "first deallocation should succeed");
    
    // Second free - should fail (double-free)
    result = track_deallocation(tracker, ptr);
    ASSERT_FALSE(result, "second deallocation should fail (double-free)");
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.total_allocations, "should have 1 allocation");
    ASSERT_EQ(1, stats.total_deallocations, "should have 1 deallocation");
    ASSERT_EQ(0, stats.active_allocations, "should have no active allocations");
    
    tracker_destroy(tracker);
    free(ptr);  // Actually free it
    
    return TEST_PASS;
}

/**
 * Test: Invalid-free detection (never allocated)
 */
static int test_invalid_free_detection(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Try to free a pointer we never allocated
    void* fake_ptr = (void*)0xDEADBEEF;
    
    bool result = track_deallocation(tracker, fake_ptr);
    ASSERT_FALSE(result, "deallocation of untracked pointer should fail");
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(0, stats.total_allocations, "should have no allocations");
    ASSERT_EQ(0, stats.total_deallocations, "should have no deallocations");
    ASSERT_EQ(1, stats.unknown_frees, "should count unknown free");
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: NULL pointer allocation handling
 */
static int test_null_allocation(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Try to track a NULL pointer
    bool result = track_allocation(tracker, NULL, 100, __FILE__, __LINE__);
    ASSERT_FALSE(result, "tracking NULL pointer should fail");
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(0, stats.total_allocations, "should have no allocations");
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: NULL pointer deallocation handling
 */
static int test_null_deallocation(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Try to free NULL - should handle gracefully
    (void)track_deallocation(tracker, NULL);
    
    // Note: freeing NULL is valid in C, but tracking it may or may not work
    // The key is that it doesn't crash
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Tracker operations with NULL tracker
 */
static int test_null_tracker_operations(void) {
    // All operations should handle NULL tracker gracefully
    void* ptr = malloc(100);
    
    // These should not crash
    bool result = track_allocation(NULL, ptr, 100, __FILE__, __LINE__);
    ASSERT_FALSE(result, "track_allocation with NULL tracker should fail");
    
    result = track_deallocation(NULL, ptr);
    ASSERT_FALSE(result, "track_deallocation with NULL tracker should fail");
    
    tracker_stats_t stats = {0};
    tracker_get_stats(NULL, &stats);  // Should not crash
    
    tracker_destroy(NULL);  // Should not crash
    
    free(ptr);
    
    return TEST_PASS;
}

/**
 * Test: Zero-size allocation
 */
static int test_zero_size_allocation(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // malloc(0) behavior is implementation-defined
    void* ptr = malloc(0);
    
    if (ptr != NULL) {
        // Track zero-size allocation
        bool result = track_allocation(tracker, ptr, 0, __FILE__, __LINE__);
        ASSERT_TRUE(result, "should track zero-size allocation");
        
        tracker_stats_t stats;
        tracker_get_stats(tracker, &stats);
        
        ASSERT_EQ(1, stats.active_allocations, "should have 1 allocation");
        ASSERT_EQ(0, stats.active_bytes, "should have 0 bytes");
        
        track_deallocation(tracker, ptr);
        free(ptr);
    }
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Very large allocation size tracking
 */
static int test_large_size_tracking(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Track a simulated very large allocation (don't actually allocate)
    void* fake_ptr = (void*)0x12345678;
    size_t huge_size = (size_t)1024 * 1024 * 1024 * 4ULL;  // 4GB
    
    bool result = track_allocation(tracker, fake_ptr, huge_size, __FILE__, __LINE__);
    ASSERT_TRUE(result, "should track large size");
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(huge_size, stats.active_bytes, "should track large byte count");
    
    track_deallocation(tracker, fake_ptr);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Repeated allocation/deallocation of same address
 */
static int test_address_reuse(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // This simulates what happens when malloc reuses an address
    for (int i = 0; i < 10; i++) {
        void* ptr = malloc(64);
        ASSERT_NOT_NULL(ptr, "malloc should succeed");
        
        track_allocation(tracker, ptr, 64, __FILE__, __LINE__);
        track_deallocation(tracker, ptr);
        free(ptr);
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(10, stats.total_allocations, "should have 10 allocations");
    ASSERT_EQ(10, stats.total_deallocations, "should have 10 deallocations");
    ASSERT_EQ(0, stats.active_allocations, "should have no active allocations");
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Allocation at boundary addresses
 */
static int test_boundary_addresses(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Test with various boundary-like addresses (simulated)
    void* addresses[] = {
        (void*)0x1,
        (void*)0xFF,
        (void*)0x100,
        (void*)0xFFFF,
        (void*)0x10000,
        (void*)0xFFFFFFFF,
    };
    
    for (size_t i = 0; i < sizeof(addresses)/sizeof(addresses[0]); i++) {
        bool result = track_allocation(tracker, addresses[i], 32, __FILE__, __LINE__);
        ASSERT_TRUE(result, "should track boundary address");
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(6, stats.active_allocations, "should have 6 allocations");
    
    // Clean up
    for (size_t i = 0; i < sizeof(addresses)/sizeof(addresses[0]); i++) {
        track_deallocation(tracker, addresses[i]);
    }
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Allocation with NULL file name
 */
static int test_null_file_name(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptr = malloc(64);
    ASSERT_NOT_NULL(ptr, "malloc should succeed");
    
    // Track with NULL file name
    bool result = track_allocation(tracker, ptr, 64, NULL, 0);
    ASSERT_TRUE(result, "should handle NULL file name");
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.active_allocations, "should have 1 allocation");
    
    track_deallocation(tracker, ptr);
    tracker_destroy(tracker);
    free(ptr);
    
    return TEST_PASS;
}

/**
 * Test: Rapid create/destroy of trackers
 */
static int test_rapid_tracker_lifecycle(void) {
    for (int i = 0; i < 100; i++) {
        memory_tracker_t* tracker = tracker_create();
        ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
        
        // Do a quick allocation
        void* ptr = malloc(32);
        if (ptr) {
            track_allocation(tracker, ptr, 32, __FILE__, __LINE__);
            track_deallocation(tracker, ptr);
            free(ptr);
        }
        
        tracker_destroy(tracker);
    }
    
    return TEST_PASS;
}

/**
 * Test: Maximum allocation count stress
 */
static int test_max_allocation_count(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const int count = 10000;
    void** ptrs = malloc((size_t)count * sizeof(void*));
    ASSERT_NOT_NULL(ptrs, "array allocation should succeed");
    
    // Allocate many
    int allocated = 0;
    for (int i = 0; i < count; i++) {
        ptrs[i] = malloc(16);
        if (ptrs[i]) {
            track_allocation(tracker, ptrs[i], 16, __FILE__, __LINE__);
            allocated++;
        }
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(allocated, (int)stats.active_allocations, "should track all allocations");
    
    // Free all
    for (int i = 0; i < count; i++) {
        if (ptrs[i]) {
            track_deallocation(tracker, ptrs[i]);
            free(ptrs[i]);
        }
    }
    
    tracker_get_stats(tracker, &stats);
    ASSERT_EQ(0, stats.active_allocations, "should have no active allocations");
    
    free(ptrs);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Interleaved allocations and deallocations
 */
static int test_interleaved_ops(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* ptrs[100];
    memset(ptrs, 0, sizeof(ptrs));
    
    // Interleaved pattern: alloc, alloc, free, alloc, free, free, ...
    int alloc_idx = 0;
    int free_idx = 0;
    
    for (int i = 0; i < 200; i++) {
        if (i % 3 != 2 && alloc_idx < 100) {
            // Allocate
            ptrs[alloc_idx] = malloc(32);
            if (ptrs[alloc_idx]) {
                track_allocation(tracker, ptrs[alloc_idx], 32, __FILE__, __LINE__);
                alloc_idx++;
            }
        } else if (free_idx < alloc_idx) {
            // Free
            track_deallocation(tracker, ptrs[free_idx]);
            free(ptrs[free_idx]);
            ptrs[free_idx] = NULL;
            free_idx++;
        }
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // Verify consistency
    ASSERT_EQ(alloc_idx, (int)stats.total_allocations, "allocation count should match");
    ASSERT_EQ(free_idx, (int)stats.total_deallocations, "deallocation count should match");
    ASSERT_EQ(alloc_idx - free_idx, (int)stats.active_allocations, "active should be difference");
    
    // Clean up remaining
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) {
            track_deallocation(tracker, ptrs[i]);
            free(ptrs[i]);
        }
    }
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Stats accuracy after many operations
 */
static int test_stats_accuracy(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    size_t total_allocated = 0;
    size_t total_freed = 0;
    int alloc_count = 0;
    int free_count = 0;
    
    void* ptrs[50];
    size_t sizes[50];
    
    // Random-ish pattern of allocations
    for (int i = 0; i < 50; i++) {
        sizes[i] = (size_t)((i * 17 + 11) % 256 + 1);  // Pseudo-random sizes 1-256
        ptrs[i] = malloc(sizes[i]);
        if (ptrs[i]) {
            track_allocation(tracker, ptrs[i], sizes[i], __FILE__, __LINE__);
            total_allocated += sizes[i];
            alloc_count++;
        }
    }
    
    // Free every other one
    for (int i = 0; i < 50; i += 2) {
        if (ptrs[i]) {
            track_deallocation(tracker, ptrs[i]);
            free(ptrs[i]);
            total_freed += sizes[i];
            free_count++;
            ptrs[i] = NULL;
        }
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(alloc_count, (int)stats.total_allocations, "allocation count should match");
    ASSERT_EQ(free_count, (int)stats.total_deallocations, "deallocation count should match");
    ASSERT_EQ(total_allocated, stats.total_bytes_allocated, "total bytes should match");
    ASSERT_EQ(total_freed, stats.total_bytes_freed, "freed bytes should match");
    ASSERT_EQ(total_allocated - total_freed, stats.active_bytes, "active bytes should match");
    
    // Clean up
    for (int i = 0; i < 50; i++) {
        if (ptrs[i]) {
            track_deallocation(tracker, ptrs[i]);
            free(ptrs[i]);
        }
    }
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

// ============================================================================
// Test Suite Definition
// ============================================================================

static test_case_t error_condition_tests[] = {
    {"double_free_detection", "Detect double-free attempts", test_double_free_detection},
    {"invalid_free_detection", "Detect free of untracked pointer", test_invalid_free_detection},
    {"null_allocation", "Handle NULL pointer allocation", test_null_allocation},
    {"null_deallocation", "Handle NULL pointer deallocation", test_null_deallocation},
    {"null_tracker_operations", "Handle operations with NULL tracker", test_null_tracker_operations},
    {"zero_size_allocation", "Handle zero-size allocations", test_zero_size_allocation},
    {"large_size_tracking", "Track very large allocation sizes", test_large_size_tracking},
    {"address_reuse", "Handle address reuse correctly", test_address_reuse},
    {"boundary_addresses", "Track boundary-like addresses", test_boundary_addresses},
    {"null_file_name", "Handle NULL file name in tracking", test_null_file_name},
    {"rapid_tracker_lifecycle", "Rapid tracker create/destroy", test_rapid_tracker_lifecycle},
    {"max_allocation_count", "Stress test with 10000 allocations", test_max_allocation_count},
    {"interleaved_ops", "Interleaved allocation/deallocation", test_interleaved_ops},
    {"stats_accuracy", "Verify statistics accuracy", test_stats_accuracy},
};

int main(void) {
    test_suite_t suite = {
        .name = "Error Condition Integration Tests",
        .tests = error_condition_tests,
        .test_count = sizeof(error_condition_tests) / sizeof(error_condition_tests[0]),
    };
    
    int result = run_test_suite(&suite);
    
    return (result == TEST_PASS) ? 0 : 1;
}
