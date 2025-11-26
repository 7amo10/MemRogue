#include "memrogue_hash_table.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NUM_THREADS 10
#define ALLOCS_PER_THREAD 10000

typedef struct {
    int thread_id;
    hash_table_t* ht;
} thread_arg_t;

void* thread_func(void* arg) {
    thread_arg_t* t_arg = (thread_arg_t*)arg;
    hash_table_t* ht = t_arg->ht;
    int id = t_arg->thread_id;

    // Use a base address offset by thread ID to avoid collisions between threads
    // (though the hash table should handle collisions, we want to verify concurrent access)
    // We'll simulate pointers using integers cast to void*
    // Start at 0x1000 to avoid NULL and low addresses
    uintptr_t base_addr = 0x1000 + (uintptr_t)id * ALLOCS_PER_THREAD * 16;

    for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
        void* ptr = (void*)(base_addr + (uintptr_t)i * 8);
        
        // Insert
        bool inserted = hash_table_insert(ht, ptr, (size_t)i, "thread_test.c", i);
        if (!inserted) {
            fprintf(stderr, "Thread %d failed to insert %p\n", id, ptr);
            return NULL;
        }

        // Lookup verification (sometimes)
        if (i % 100 == 0) {
            allocation_info_t* info = hash_table_lookup(ht, ptr);
            if (!info || info->size != (size_t)i) {
                fprintf(stderr, "Thread %d failed lookup verification for %p\n", id, ptr);
                return NULL;
            }
        }
    }

    // Verify count roughly (hard to do exactly without locking the whole thing, 
    // but we know at least ALLOCS_PER_THREAD items should be there from this thread)
    
    // Remove half of them
    for (int i = 0; i < ALLOCS_PER_THREAD; i += 2) {
        void* ptr = (void*)(base_addr + (uintptr_t)i * 8);
        bool removed = hash_table_remove(ht, ptr);
        if (!removed) {
            fprintf(stderr, "Thread %d failed to remove %p\n", id, ptr);
            return NULL;
        }
    }

    return NULL;
}

int main() {
    printf("Starting multithreaded test (%d threads, %d allocs each)...\n", NUM_THREADS, ALLOCS_PER_THREAD);

    hash_table_t* ht = hash_table_create(4096); // Start with decent size to reduce initial resizing contention
    assert(ht != NULL);

    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i) {
        args[i].thread_id = i;
        args[i].ht = ht;
        if (pthread_create(&threads[i], NULL, thread_func, &args[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    // Verify final count
    // Each thread inserted ALLOCS_PER_THREAD and removed half (ALLOCS_PER_THREAD / 2)
    // So remaining should be NUM_THREADS * (ALLOCS_PER_THREAD / 2)
    size_t expected = NUM_THREADS * (ALLOCS_PER_THREAD / 2);
    size_t actual = hash_table_count(ht);
    
    printf("Expected items: %zu, Actual items: %zu\n", expected, actual);
    assert(actual == expected);

    // Cleanup remaining items
    hash_table_destroy(ht);

    printf("Multithreaded test passed!\n");
    return 0;
}
