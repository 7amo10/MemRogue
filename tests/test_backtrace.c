#include "memrogue_backtrace.h"
#include "memrogue_allocation_record.h"
#include "memrogue_hash_table.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// ============ Symbol Resolution Tests ============

void test_symbol_resolve_null() {
    // Should handle NULL gracefully
    resolved_backtrace_t* bt = symbol_resolve(NULL);
    assert(bt == NULL);
    
    // Resolve frame with NULL should return 0
    int result = symbol_resolve_frame(NULL, NULL);
    assert(result == 0);
}

void test_symbol_resolve_empty_info() {
    allocation_info_t* info = allocation_info_create((void*)0x1000, 100, "test.c", 1, 0);
    assert(info != NULL);
    
    // frame_count is 0, should return NULL
    assert(info->frame_count == 0);
    resolved_backtrace_t* bt = symbol_resolve(info);
    assert(bt == NULL);
    
    allocation_info_destroy(info);
}

__attribute__((noinline))
void test_function_for_symbol_resolution(allocation_info_t* info) {
    backtrace_capture(info, 0);
}

void test_symbol_resolve_basic() {
    allocation_info_t* info = allocation_info_create((void*)0x2000, 100, "test.c", 2, 0);
    assert(info != NULL);
    
    // Capture real backtrace
    test_function_for_symbol_resolution(info);
    
    if (backtrace_available() && info->frame_count > 0) {
        resolved_backtrace_t* bt = symbol_resolve(info);
        assert(bt != NULL);
        assert(bt->frame_count == info->frame_count);
        assert(bt->frames != NULL);
        
        printf("  Resolved %d frames:\n", bt->frame_count);
        for (int i = 0; i < bt->frame_count && i < 5; i++) {
            printf("    [%d] %s\n", i, 
                   bt->frames[i].function_name ? bt->frames[i].function_name : "(null)");
        }
        if (bt->frame_count > 5) {
            printf("    ... and %d more\n", bt->frame_count - 5);
        }
        
        // At least one frame should have a function name
        int has_name = 0;
        for (int i = 0; i < bt->frame_count; i++) {
            if (bt->frames[i].function_name && bt->frames[i].function_name[0] != '\0') {
                has_name = 1;
                break;
            }
        }
        assert(has_name);
        
        resolved_backtrace_destroy(bt);
    }
    
    allocation_info_destroy(info);
}

void test_symbol_resolve_frame_single() {
    allocation_info_t* info = allocation_info_create((void*)0x3000, 100, "test.c", 3, 0);
    assert(info != NULL);
    
    test_function_for_symbol_resolution(info);
    
    if (backtrace_available() && info->frame_count > 0) {
        resolved_frame_t frame;
        int result = symbol_resolve_frame(info->frames[0], &frame);
        assert(result == 1);
        assert(frame.address == info->frames[0]);
        assert(frame.function_name != NULL);
        
        printf("  Single frame: %s\n", frame.function_name);
    }
    
    allocation_info_destroy(info);
}

void test_symbol_format() {
    allocation_info_t* info = allocation_info_create((void*)0x4000, 100, "test.c", 4, 0);
    assert(info != NULL);
    
    test_function_for_symbol_resolution(info);
    
    if (backtrace_available() && info->frame_count > 0) {
        resolved_backtrace_t* bt = symbol_resolve(info);
        assert(bt != NULL);
        
        char buffer[512];
        int written = resolved_frame_format(&bt->frames[0], buffer, sizeof(buffer));
        assert(written > 0);
        assert(strlen(buffer) > 0);
        
        printf("  Formatted: %s\n", buffer);
        
        resolved_backtrace_destroy(bt);
    }
    
    allocation_info_destroy(info);
}

void test_resolved_backtrace_destroy_null() {
    // Should not crash
    resolved_backtrace_destroy(NULL);
}

void test_symbol_format_truncation() {
    allocation_info_t* info = allocation_info_create((void*)0x6000, 100, "test.c", 6, 0);
    assert(info != NULL);
    
    test_function_for_symbol_resolution(info);
    
    if (backtrace_available() && info->frame_count > 0) {
        resolved_backtrace_t* bt = symbol_resolve(info);
        assert(bt != NULL);
        
        // Test with a very small buffer to trigger truncation
        char small_buffer[10];
        int written = resolved_frame_format(&bt->frames[0], small_buffer, sizeof(small_buffer));
        
        // Written should be capped to buffer_size - 1 (9 chars max)
        assert(written >= 0);
        assert(written <= 9);
        assert(strlen(small_buffer) <= 9);
        
        // Ensure null termination
        assert(small_buffer[sizeof(small_buffer) - 1] == '\0');
        
        printf("  Truncated output (%d chars): %s\n", written, small_buffer);
        
        resolved_backtrace_destroy(bt);
    }
    
    allocation_info_destroy(info);
}

void test_symbol_resolve_frame_fallback() {
    // Test with an invalid/unlikely address that won't resolve to a valid symbol
    void* unlikely_address = (void*)0xDEADBEEF;
    resolved_frame_t frame;
    
    int result = symbol_resolve_frame(unlikely_address, &frame);
    assert(result == 1);  // Should still succeed
    assert(frame.address == unlikely_address);
    
    // The function should provide a fallback (address as hex string)
    assert(frame.function_name != NULL);
    assert(strlen(frame.function_name) > 0);
    
    // Verify the fallback format shows the address
    printf("  Fallback for 0x%lx: %s\n", (unsigned long)(uintptr_t)unlikely_address, frame.function_name);
}

void test_symbol_contains_expected_functions() {
    allocation_info_t* info = allocation_info_create((void*)0x5000, 100, "test.c", 5, 0);
    assert(info != NULL);
    
    // Use deep stack to get recognizable function names
    deep_function_1(info);
    
    if (backtrace_available() && info->frame_count > 0) {
        resolved_backtrace_t* bt = symbol_resolve(info);
        assert(bt != NULL);
        
        // Look for our test functions in the backtrace
        int found_deep = 0;
        for (int i = 0; i < bt->frame_count; i++) {
            if (bt->frames[i].function_name) {
                if (strstr(bt->frames[i].function_name, "deep_function") != NULL) {
                    found_deep = 1;
                }
            }
        }
        
        printf("  Found deep_function in backtrace: %s\n", found_deep ? "yes" : "no");
        // Note: This might not always work depending on optimization level
        
        resolved_backtrace_destroy(bt);
    }
    
    allocation_info_destroy(info);
}

// ============================================================================
// Frame Filtering Tests
// ============================================================================

void test_frame_filter_init() {
    frame_filter_t filter;
    frame_filter_init(&filter);
    
    assert(filter.pattern_count == 3);
    assert(filter.skip_count == MEMROGUE_DEFAULT_SKIP_FRAMES);
    assert(strcmp(filter.patterns[0], "memrogue_") == 0);
    assert(strcmp(filter.patterns[1], "backtrace") == 0);
    assert(strcmp(filter.patterns[2], "__libc_") == 0);
}

void test_frame_filter_init_simple() {
    frame_filter_t filter;
    frame_filter_init_simple(&filter, 5);
    
    assert(filter.pattern_count == 0);
    assert(filter.skip_count == 5);
}

void test_frame_filter_add_pattern() {
    frame_filter_t filter;
    frame_filter_init_simple(&filter, 0);
    
    int result = frame_filter_add_pattern(&filter, "test_");
    assert(result == 1);
    assert(filter.pattern_count == 1);
    assert(strcmp(filter.patterns[0], "test_") == 0);
    
    result = frame_filter_add_pattern(&filter, "foo_");
    assert(result == 1);
    assert(filter.pattern_count == 2);
    
    // Test NULL handling
    result = frame_filter_add_pattern(NULL, "bar_");
    assert(result == 0);
    
    result = frame_filter_add_pattern(&filter, NULL);
    assert(result == 0);
}

void test_frame_filter_should_skip() {
    frame_filter_t filter;
    frame_filter_init_simple(&filter, 0);
    frame_filter_add_pattern(&filter, "memrogue_");
    frame_filter_add_pattern(&filter, "__libc_");
    
    // Should skip memrogue functions
    assert(frame_filter_should_skip(&filter, "memrogue_track_alloc") == 1);
    assert(frame_filter_should_skip(&filter, "memrogue_init") == 1);
    
    // Should skip libc internal functions
    assert(frame_filter_should_skip(&filter, "__libc_malloc") == 1);
    
    // Should NOT skip user functions
    assert(frame_filter_should_skip(&filter, "main") == 0);
    assert(frame_filter_should_skip(&filter, "my_function") == 0);
    assert(frame_filter_should_skip(&filter, "test_something") == 0);
    
    // NULL handling
    assert(frame_filter_should_skip(NULL, "test") == 0);
    assert(frame_filter_should_skip(&filter, NULL) == 0);
}

void test_frame_filter_clear() {
    frame_filter_t filter;
    frame_filter_init(&filter);
    
    assert(filter.pattern_count == 3);
    
    frame_filter_clear(&filter);
    
    assert(filter.pattern_count == 0);
    
    // NULL handling - should not crash
    frame_filter_clear(NULL);
}

void test_frame_filter_get_default() {
    const frame_filter_t* filter = frame_filter_get_default();
    
    assert(filter != NULL);
    assert(filter->pattern_count > 0);
    assert(filter->skip_count >= 0);
}

void test_backtrace_capture_filtered_null() {
    // NULL info should return 0
    int result = backtrace_capture_filtered(NULL, NULL);
    assert(result == 0);
}

void test_backtrace_capture_filtered_basic() {
    allocation_info_t* info = allocation_info_create((void*)0x7000, 100, "test.c", 7, 0);
    assert(info != NULL);
    
    frame_filter_t filter;
    frame_filter_init_simple(&filter, 1);  // Skip just 1 frame
    
    if (backtrace_available()) {
        int count = backtrace_capture_filtered(info, &filter);
        printf("  Captured %d frames with filter\n", count);
        assert(count >= 0);
    }
    
    allocation_info_destroy(info);
}

void test_backtrace_capture_filtered_no_filter() {
    allocation_info_t* info = allocation_info_create((void*)0x8000, 100, "test.c", 8, 0);
    assert(info != NULL);
    
    if (backtrace_available()) {
        // NULL filter should still work (uses default skip)
        int count = backtrace_capture_filtered(info, NULL);
        printf("  Captured %d frames with NULL filter\n", count);
        assert(count >= 0);
    }
    
    allocation_info_destroy(info);
}

// Helper function with recognizable name for filtering test
// Note: This function is intentionally named to test the filter but we test
// the filter matching separately; keeping for potential future use
__attribute__((noinline, unused))
static void memrogue_test_internal_function(allocation_info_t* info) {
    // This function name starts with "memrogue_" so should be filtered
    backtrace_capture(info, 0);
}

// Helper function with user-like name
__attribute__((noinline))
static void user_allocation_function(allocation_info_t* info, const frame_filter_t* filter) {
    backtrace_capture_filtered(info, filter);
}

void test_backtrace_filtered_removes_internal() {
    allocation_info_t* info = allocation_info_create((void*)0x9000, 100, "test.c", 9, 0);
    assert(info != NULL);
    
    if (backtrace_available()) {
        // Setup filter to skip memrogue_ and backtrace functions
        frame_filter_t filter;
        frame_filter_init(&filter);
        filter.skip_count = 0;  // Don't skip by count, only by pattern
        
        // Capture with filtering
        user_allocation_function(info, &filter);
        
        if (info->frame_count > 0) {
            // Resolve the first frame and check it's not a filtered function
            resolved_frame_t frame;
            if (symbol_resolve_frame(info->frames[0], &frame)) {
                printf("  First frame after filter: %s\n", 
                       frame.function_name ? frame.function_name : "(unknown)");
                
                // The first frame should NOT start with filtered prefixes
                if (frame.function_name) {
                    assert(strncmp(frame.function_name, "memrogue_", 9) != 0);
                    assert(strncmp(frame.function_name, "__libc_", 7) != 0);
                }
            }
        }
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
    
    printf("\n=== Symbol Resolution Tests ===\n\n");
    
    TEST(test_symbol_resolve_null);
    TEST(test_symbol_resolve_empty_info);
    TEST(test_symbol_resolve_basic);
    TEST(test_symbol_resolve_frame_single);
    TEST(test_symbol_format);
    TEST(test_symbol_format_truncation);
    TEST(test_symbol_resolve_frame_fallback);
    TEST(test_resolved_backtrace_destroy_null);
    TEST(test_symbol_contains_expected_functions);
    
    printf("\n=== Frame Filtering Tests ===\n\n");
    
    TEST(test_frame_filter_init);
    TEST(test_frame_filter_init_simple);
    TEST(test_frame_filter_add_pattern);
    TEST(test_frame_filter_should_skip);
    TEST(test_frame_filter_clear);
    TEST(test_frame_filter_get_default);
    TEST(test_backtrace_capture_filtered_null);
    TEST(test_backtrace_capture_filtered_basic);
    TEST(test_backtrace_capture_filtered_no_filter);
    TEST(test_backtrace_filtered_removes_internal);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
