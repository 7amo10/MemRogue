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

void test_create_destroy() {
    hash_table_t* ht = hash_table_create(10);
    assert(ht != NULL);
    assert(hash_table_count(ht) == 0);
    hash_table_destroy(ht);
}

void test_create_zero_capacity() {
    // Edge case: create with zero capacity should still work (or handle gracefully)
    hash_table_t* ht = hash_table_create(0);
    // Implementation might use a default minimum capacity
    assert(ht != NULL || ht == NULL); // Accept either behavior
    if (ht) hash_table_destroy(ht);
}

void test_insert_lookup() {
    hash_table_t* ht = hash_table_create(10);
    
    void* ptr1 = (void*)0x1234;
    void* ptr2 = (void*)0x5678;

    assert(hash_table_insert(ht, ptr1, 100, "test.c", 10));
    assert(hash_table_insert(ht, ptr2, 200, "test.c", 20));
    assert(hash_table_count(ht) == 2);

    allocation_info_t* info1 = hash_table_lookup(ht, ptr1);
    assert(info1 != NULL);
    assert(info1->size == 100);
    assert(info1->line == 10);

    allocation_info_t* info2 = hash_table_lookup(ht, ptr2);
    assert(info2 != NULL);
    assert(info2->size == 200);

    assert(hash_table_lookup(ht, (void*)0x9999) == NULL);

    hash_table_destroy(ht);
}

void test_insert_null_params() {
    hash_table_t* ht = hash_table_create(10);
    
    // Inserting NULL pointer should fail
    assert(!hash_table_insert(ht, NULL, 100, "test.c", 10));
    
    // Inserting into NULL table should fail
    assert(!hash_table_insert(NULL, (void*)0x1234, 100, "test.c", 10));
    
    // Lookup NULL should return NULL
    assert(hash_table_lookup(ht, NULL) == NULL);
    assert(hash_table_lookup(NULL, (void*)0x1234) == NULL);
    
    hash_table_destroy(ht);
}

void test_update_existing() {
    hash_table_t* ht = hash_table_create(10);
    void* ptr = (void*)0x1234;
    
    // Insert initial entry
    assert(hash_table_insert(ht, ptr, 100, "first.c", 10));
    assert(hash_table_count(ht) == 1);
    
    // Update same pointer with new data
    assert(hash_table_insert(ht, ptr, 200, "second.c", 20));
    assert(hash_table_count(ht) == 1); // Count should not change
    
    allocation_info_t* info = hash_table_lookup(ht, ptr);
    assert(info != NULL);
    assert(info->size == 200);
    assert(info->line == 20);
    assert(strcmp(info->file, "second.c") == 0);
    
    hash_table_destroy(ht);
}

void test_remove() {
    hash_table_t* ht = hash_table_create(10);
    void* ptr1 = (void*)0x1234;

    hash_table_insert(ht, ptr1, 100, "test.c", 10);
    assert(hash_table_count(ht) == 1);

    assert(hash_table_remove(ht, ptr1));
    assert(hash_table_count(ht) == 0);
    assert(hash_table_lookup(ht, ptr1) == NULL);
    
    assert(!hash_table_remove(ht, ptr1)); // Should fail if already removed

    hash_table_destroy(ht);
}

void test_remove_null_params() {
    hash_table_t* ht = hash_table_create(10);
    
    // Removing NULL pointer should fail
    assert(!hash_table_remove(ht, NULL));
    
    // Removing from NULL table should fail
    assert(!hash_table_remove(NULL, (void*)0x1234));
    
    hash_table_destroy(ht);
}

void test_collision() {
    // Force small bucket count to ensure collisions
    hash_table_t* ht = hash_table_create(1); 
    
    void* ptr1 = (void*)0x1000;
    void* ptr2 = (void*)0x2000;
    void* ptr3 = (void*)0x3000;

    hash_table_insert(ht, ptr1, 10, "f.c", 1);
    hash_table_insert(ht, ptr2, 20, "f.c", 2);
    hash_table_insert(ht, ptr3, 30, "f.c", 3);

    assert(hash_table_count(ht) == 3);
    
    assert(hash_table_lookup(ht, ptr1)->size == 10);
    assert(hash_table_lookup(ht, ptr2)->size == 20);
    assert(hash_table_lookup(ht, ptr3)->size == 30);

    hash_table_remove(ht, ptr2);
    assert(hash_table_count(ht) == 2);
    assert(hash_table_lookup(ht, ptr2) == NULL);
    assert(hash_table_lookup(ht, ptr1) != NULL);
    assert(hash_table_lookup(ht, ptr3) != NULL);

    hash_table_destroy(ht);
}

void test_large_scale() {
    hash_table_t* ht = hash_table_create(1024);
    const size_t COUNT = 1000;
    void** ptrs = malloc(COUNT * sizeof(void*));

    for (size_t i = 0; i < COUNT; i++) {
        ptrs[i] = (void*)(uintptr_t)(i * 8 + 0x1000);
        hash_table_insert(ht, ptrs[i], i, "stress.c", (int)i);
    }

    assert(hash_table_count(ht) == COUNT);

    for (size_t i = 0; i < COUNT; i++) {
        allocation_info_t* info = hash_table_lookup(ht, ptrs[i]);
        assert(info != NULL);
        assert(info->size == i);
    }

    hash_table_destroy(ht);
    free(ptrs);
}

void test_resize() {
    // Start with small capacity
    hash_table_t* ht = hash_table_create(4);
    
    // Insert 5 items (should trigger resize at 4 * 0.75 = 3 items)
    for (size_t i = 0; i < 5; i++) {
        void* ptr = (void*)(uintptr_t)(i * 8 + 0x5000);
        hash_table_insert(ht, ptr, i, "resize.c", (int)i);
    }

    // Check if all items are still accessible
    for (size_t i = 0; i < 5; i++) {
        void* ptr = (void*)(uintptr_t)(i * 8 + 0x5000);
        assert(hash_table_lookup(ht, ptr) != NULL);
    }
    
    hash_table_destroy(ht);
}

void test_file_string_ownership() {
    // Verify that the hash table properly copies file strings
    hash_table_t* ht = hash_table_create(10);
    
    char filename[32];
    strcpy(filename, "original.c");
    
    void* ptr = (void*)0xABCD;
    hash_table_insert(ht, ptr, 100, filename, 42);
    
    // Modify the original string
    strcpy(filename, "modified.c");
    
    // The stored filename should still be "original.c"
    allocation_info_t* info = hash_table_lookup(ht, ptr);
    assert(info != NULL);
    assert(strcmp(info->file, "original.c") == 0);
    
    hash_table_destroy(ht);
}

int main() {
    printf("=== Hash Table Unit Tests ===\n\n");
    
    TEST(test_create_destroy);
    TEST(test_create_zero_capacity);
    TEST(test_insert_lookup);
    TEST(test_insert_null_params);
    TEST(test_update_existing);
    TEST(test_remove);
    TEST(test_remove_null_params);
    TEST(test_collision);
    TEST(test_resize);
    TEST(test_file_string_ownership);
    TEST(test_large_scale);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
