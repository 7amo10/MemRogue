#include "memrogue_backtrace.h"
#include "memrogue_allocation_record.h"
#include "memrogue_hash_table.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("Testing %s...\n", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf("Passed.\n"); \
} while(0)

void test_backtrace_available() {
    // Just verify the function doesn't crash
    int available = backtrace_available();
    // On Linux/glibc, it should be available
    printf("  Backtrace available: %s\n", available ? "yes" : "no");
#if defined(__GLIBC__) || defined(__APPLE__)
    assert(available == 1);
#endif
}

void test_backtrace_capture_basic() {
    allocation_info_t* info = allocation_info_create((void*)0x1234, 100, "test.c", 42, 0);
    assert(info != NULL);
    
    // Initially, frame_count should be 0
    assert(info->frame_count == 0);
    
    // Capture backtrace
    int frames = backtrace_capture(info, 0);
    
    if (backtrace_available()) {
        assert(frames > 0);
        assert(info->frame_count > 0);
        assert(info->frame_count == frames);
        
        // At least one frame should be non-NULL
        int has_valid_frame = 0;
        for (int i = 0; i < info->frame_count; i++) {
            if (info->frames[i] != NULL) {
                has_valid_frame = 1;
                break;
            }
        }
        assert(has_valid_frame);
        
        printf("  Captured %d frames\n", frames);
    }
    
    allocation_info_destroy(info);
}

void test_backtrace_capture_null_info() {
    // Should handle NULL gracefully
    int frames = backtrace_capture(NULL, 0);
    assert(frames == 0);
}

void test_backtrace_skip_frames() {
    allocation_info_t* info1 = allocation_info_create((void*)0x1000, 100, "test.c", 1, 0);
    allocation_info_t* info2 = allocation_info_create((void*)0x2000, 100, "test.c", 2, 0);
    
    assert(info1 != NULL);
    assert(info2 != NULL);
    
    // Capture with no skip
    int frames1 = backtrace_capture(info1, 0);
    
    // Capture with skip
    int frames2 = backtrace_capture(info2, 2);
    
    if (backtrace_available() && frames1 > 2) {
        // frames2 should have fewer frames (or equal if we hit the max)
        assert(frames2 <= frames1);
        printf("  Frames without skip: %d, with skip(2): %d\n", frames1, frames2);
    }
    
    allocation_info_destroy(info1);
    allocation_info_destroy(info2);
}

void test_backtrace_max_frames() {
    allocation_info_t* info = allocation_info_create((void*)0x3000, 100, "test.c", 3, 0);
    assert(info != NULL);
    
    backtrace_capture(info, 0);
    
    if (backtrace_available()) {
        // Should not exceed MEMROGUE_MAX_FRAMES
        assert(info->frame_count <= MEMROGUE_MAX_FRAMES);
        printf("  Max frames limit respected: %d <= %d\n", info->frame_count, MEMROGUE_MAX_FRAMES);
    }
    
    allocation_info_destroy(info);
}

// Helper function to create deeper call stack
__attribute__((noinline)) 
void deep_function_3(allocation_info_t* info) {
    backtrace_capture(info, 0);
}

__attribute__((noinline))
void deep_function_2(allocation_info_t* info) {
    deep_function_3(info);
}

__attribute__((noinline))
void deep_function_1(allocation_info_t* info) {
    deep_function_2(info);
}

void test_backtrace_deep_stack() {
    allocation_info_t* info = allocation_info_create((void*)0x4000, 100, "test.c", 4, 0);
    assert(info != NULL);
    
    // Capture from deep in the call stack
    deep_function_1(info);
    
    if (backtrace_available()) {
        // Should have captured multiple frames
        assert(info->frame_count >= 3);
        printf("  Deep stack captured %d frames\n", info->frame_count);
    }
    
    allocation_info_destroy(info);
}

void test_hash_table_insert_with_backtrace() {
    hash_table_t* ht = hash_table_create(10);
    assert(ht != NULL);
    
    void* ptr = (void*)0x5000;
    
    // Insert with backtrace capture
    bool result = hash_table_insert_with_backtrace(ht, ptr, 256, "test.c", 100, 0);
    assert(result == true);
    
    // Lookup and verify backtrace was captured
    allocation_info_t* info = hash_table_lookup(ht, ptr);
    assert(info != NULL);
    assert(info->ptr == ptr);
    assert(info->size == 256);
    
    if (backtrace_available()) {
        assert(info->frame_count > 0);
        printf("  Hash table insert captured %d frames\n", info->frame_count);
    }
    
    hash_table_destroy(ht);
}

void test_backtrace_reinitialize() {
    allocation_info_t* info = allocation_info_create((void*)0x6000, 100, "test.c", 6, 0);
    assert(info != NULL);
    
    // First capture
    backtrace_capture(info, 0);
    int first_count = info->frame_count;
    
    // Second capture should reinitialize
    backtrace_capture(info, 0);
    int second_count = info->frame_count;
    
    // Counts should be similar (same call depth)
    if (backtrace_available()) {
        assert(first_count > 0);
        assert(second_count > 0);
        printf("  Reinitialize: first=%d, second=%d\n", first_count, second_count);
    }
    
    allocation_info_destroy(info);
}

int main() {
    printf("=== Backtrace Unit Tests ===\n\n");
    
    TEST(test_backtrace_available);
    TEST(test_backtrace_capture_basic);
    TEST(test_backtrace_capture_null_info);
    TEST(test_backtrace_skip_frames);
    TEST(test_backtrace_max_frames);
    TEST(test_backtrace_deep_stack);
    TEST(test_hash_table_insert_with_backtrace);
    TEST(test_backtrace_reinitialize);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
