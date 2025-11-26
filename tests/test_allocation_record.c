#include "memrogue_allocation_record.h"
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

void test_create_basic() {
    void* ptr = (void*)0x1234;
    allocation_info_t* info = allocation_info_create(ptr, 100, "test.c", 42, 12345);
    
    assert(info != NULL);
    assert(info->ptr == ptr);
    assert(info->size == 100);
    assert(info->line == 42);
    assert(info->timestamp == 12345);
    assert(strcmp(info->file, "test.c") == 0);
    
    allocation_info_destroy(info);
}

void test_create_null_file() {
    void* ptr = (void*)0x5678;
    allocation_info_t* info = allocation_info_create(ptr, 200, NULL, 10, 0);
    
    assert(info != NULL);
    assert(info->ptr == ptr);
    assert(info->size == 200);
    assert(info->file == NULL);
    
    allocation_info_destroy(info);
}

void test_file_string_copy() {
    // Verify that the file string is copied, not just stored as pointer
    char filename[64] = "original_file.c";
    void* ptr = (void*)0xABCD;
    
    allocation_info_t* info = allocation_info_create(ptr, 50, filename, 1, 0);
    
    // Modify original string
    strcpy(filename, "modified_file.c");
    
    // The info should still have the original filename
    assert(info != NULL);
    assert(strcmp(info->file, "original_file.c") == 0);
    
    allocation_info_destroy(info);
}

void test_destroy_null() {
    // Should not crash when destroying NULL
    allocation_info_destroy(NULL);
}

void test_large_filename() {
    // Test with a very long filename
    char long_filename[512];
    memset(long_filename, 'a', sizeof(long_filename) - 1);
    long_filename[sizeof(long_filename) - 1] = '\0';
    
    void* ptr = (void*)0xDEAD;
    allocation_info_t* info = allocation_info_create(ptr, 1024, long_filename, 999, 0);
    
    assert(info != NULL);
    assert(strlen(info->file) == sizeof(long_filename) - 1);
    assert(strcmp(info->file, long_filename) == 0);
    
    allocation_info_destroy(info);
}

void test_zero_size() {
    void* ptr = (void*)0x1111;
    allocation_info_t* info = allocation_info_create(ptr, 0, "zero.c", 0, 0);
    
    assert(info != NULL);
    assert(info->size == 0);
    
    allocation_info_destroy(info);
}

void test_max_values() {
    void* ptr = (void*)(uintptr_t)-1; // Max pointer value
    size_t max_size = (size_t)-1;
    int max_line = 2147483647;
    uint64_t max_ts = (uint64_t)-1;
    
    allocation_info_t* info = allocation_info_create(ptr, max_size, "max.c", max_line, max_ts);
    
    assert(info != NULL);
    assert(info->ptr == ptr);
    assert(info->size == max_size);
    assert(info->line == max_line);
    assert(info->timestamp == max_ts);
    
    allocation_info_destroy(info);
}

int main() {
    printf("=== Allocation Record Unit Tests ===\n\n");
    
    TEST(test_create_basic);
    TEST(test_create_null_file);
    TEST(test_file_string_copy);
    TEST(test_destroy_null);
    TEST(test_large_filename);
    TEST(test_zero_size);
    TEST(test_max_values);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
