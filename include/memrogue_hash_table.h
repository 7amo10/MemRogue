#ifndef MEMROGUE_HASH_TABLE_H
#define MEMROGUE_HASH_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include "memrogue_allocation_record.h"

// Hash table node for chaining
typedef struct hash_node {
    allocation_info_t* info;
    struct hash_node* next;
} hash_node_t;

// Hash table structure
typedef struct {
    hash_node_t** buckets;
    size_t bucket_count;
    size_t item_count;
    pthread_mutex_t lock;
} hash_table_t;

// Function prototypes
hash_table_t* hash_table_create(size_t initial_capacity);
void hash_table_destroy(hash_table_t* ht);

bool hash_table_insert(hash_table_t* ht, void* ptr, size_t size, const char* file, int line);
allocation_info_t* hash_table_lookup(hash_table_t* ht, void* ptr);
bool hash_table_remove(hash_table_t* ht, void* ptr);

size_t hash_table_count(hash_table_t* ht);

// Extended insert with backtrace capture
// skip_frames: number of stack frames to skip (for internal functions)
bool hash_table_insert_with_backtrace(hash_table_t* ht, void* ptr, size_t size, 
                                       const char* file, int line, int skip_frames);

// Thread safety helpers
void hash_table_lock_acquire(hash_table_t* ht);
void hash_table_lock_release(hash_table_t* ht);

// Iteration callback type: returns true to continue, false to stop
typedef bool (*hash_table_iterate_fn)(const allocation_info_t* info, void* user_data);

// Iterate through all entries in the hash table
// callback: function called for each entry, return false to stop iteration
// user_data: passed to callback
void hash_table_iterate(hash_table_t* ht, hash_table_iterate_fn callback, void* user_data);

#endif // MEMROGUE_HASH_TABLE_H
