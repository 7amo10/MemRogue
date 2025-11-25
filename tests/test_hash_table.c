#include "memrogue_hash_table.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void test_create_destroy() {
    printf("Testing create and destroy...\n");
    hash_table_t* ht = hash_table_create(10);
    assert(ht != NULL);
    assert(hash_table_count(ht) == 0);
    hash_table_destroy(ht);
    printf("Passed.\n");
}

void test_insert_lookup() {
    printf("Testing insert and lookup...\n");
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
    printf("Passed.\n");
}

void test_remove() {
    printf("Testing remove...\n");
    hash_table_t* ht = hash_table_create(10);
    void* ptr1 = (void*)0x1234;

    hash_table_insert(ht, ptr1, 100, "test.c", 10);
    assert(hash_table_count(ht) == 1);

    assert(hash_table_remove(ht, ptr1));
    assert(hash_table_count(ht) == 0);
    assert(hash_table_lookup(ht, ptr1) == NULL);
    
    assert(!hash_table_remove(ht, ptr1)); // Should fail if already removed

    hash_table_destroy(ht);
    printf("Passed.\n");
}

void test_collision() {
    printf("Testing collision handling...\n");
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
    printf("Passed.\n");
}

void test_large_scale() {
    printf("Testing large scale (1000 entries)...\n");
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
    printf("Passed.\n");
}

void test_resize() {
    printf("Testing resize...\n");
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
    
    // Verify capacity increased (we can't check internal struct directly easily without exposing it, 
    // but if it didn't crash and items are found, it's likely fine. 
    // Ideally we would check ht->bucket_count if we could access it or add a getter)
    
    hash_table_destroy(ht);
    printf("Passed.\n");
}

int main() {
    test_create_destroy();
    test_insert_lookup();
    test_remove();
    test_collision();
    test_resize();
    test_large_scale();
    printf("All hash table tests passed!\n");
    return 0;
}
