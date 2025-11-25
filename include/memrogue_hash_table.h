#ifndef MEMROGUE_HASH_TABLE_H
#define MEMROGUE_HASH_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

// Structure to hold allocation information
typedef struct {
    void* ptr;              // Address of the allocation (Key)
    size_t size;            // Size of the allocation
    const char* file;       // Source file name
    int line;               // Line number
    uint64_t timestamp;     // Allocation timestamp (or sequence number)
    // Stack trace info will be added in later sprints
} allocation_info_t;

// Hash table node for chaining
typedef struct hash_node {
    allocation_info_t info;
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

#endif // MEMROGUE_HASH_TABLE_H
