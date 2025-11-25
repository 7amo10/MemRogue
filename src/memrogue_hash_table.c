#include "memrogue_hash_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Simple hash function for pointers
// Thomas Wang's 64-bit integer hash function
static size_t hash_ptr(void* ptr, size_t bucket_count) {
    uint64_t key = (uint64_t)(uintptr_t)ptr;
    key = (~key) + (key << 21); // key = (key << 21) - key - 1;
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8); // key * 265
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4); // key * 21
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return (size_t)(key % bucket_count);
}

hash_table_t* hash_table_create(size_t initial_capacity) {
    hash_table_t* ht = (hash_table_t*)malloc(sizeof(hash_table_t));
    if (!ht) return NULL;

    ht->bucket_count = initial_capacity > 0 ? initial_capacity : 1024;
    ht->item_count = 0;
    ht->buckets = (hash_node_t**)calloc(ht->bucket_count, sizeof(hash_node_t*));
    
    if (!ht->buckets) {
        free(ht);
        return NULL;
    }

    if (pthread_mutex_init(&ht->lock, NULL) != 0) {
        free(ht->buckets);
        free(ht);
        return NULL;
    }

    return ht;
}

void hash_table_destroy(hash_table_t* ht) {
    if (!ht) return;

    pthread_mutex_lock(&ht->lock);
    for (size_t i = 0; i < ht->bucket_count; ++i) {
        hash_node_t* current = ht->buckets[i];
        while (current) {
            hash_node_t* temp = current;
            current = current->next;
            free(temp);
        }
    }
    free(ht->buckets);
    pthread_mutex_unlock(&ht->lock);
    pthread_mutex_destroy(&ht->lock);
    free(ht);
}

bool hash_table_insert(hash_table_t* ht, void* ptr, size_t size, const char* file, int line) {
    if (!ht || !ptr) return false;

    pthread_mutex_lock(&ht->lock);

    // Resize if load factor > 0.75
    if (ht->item_count >= (ht->bucket_count * 3) / 4) {
        size_t new_capacity = ht->bucket_count * 2;
        hash_node_t** new_buckets = (hash_node_t**)calloc(new_capacity, sizeof(hash_node_t*));
        
        if (new_buckets) {
            for (size_t i = 0; i < ht->bucket_count; ++i) {
                hash_node_t* current = ht->buckets[i];
                while (current) {
                    hash_node_t* next = current->next;
                    size_t new_index = hash_ptr(current->info.ptr, new_capacity);
                    current->next = new_buckets[new_index];
                    new_buckets[new_index] = current;
                    current = next;
                }
            }
            free(ht->buckets);
            ht->buckets = new_buckets;
            ht->bucket_count = new_capacity;
        }
    }

    size_t index = hash_ptr(ptr, ht->bucket_count);
    hash_node_t* current = ht->buckets[index];

    // Check if key already exists
    while (current) {
        if (current->info.ptr == ptr) {
            // Update existing entry
            current->info.size = size;
            current->info.file = file;
            current->info.line = line;
            // timestamp update?
            pthread_mutex_unlock(&ht->lock);
            return true;
        }
        current = current->next;
    }

    // Create new node
    // NOTE: In the final interceptor, this malloc must be replaced 
    // by a custom allocator to avoid recursion.
    hash_node_t* new_node = (hash_node_t*)malloc(sizeof(hash_node_t));
    if (!new_node) {
        pthread_mutex_unlock(&ht->lock);
        return false;
    }

    new_node->info.ptr = ptr;
    new_node->info.size = size;
    new_node->info.file = file;
    new_node->info.line = line;
    new_node->info.timestamp = 0; // TODO: Add timestamp logic
    
    // Insert at head of bucket
    new_node->next = ht->buckets[index];
    ht->buckets[index] = new_node;
    ht->item_count++;

    pthread_mutex_unlock(&ht->lock);
    return true;
}

allocation_info_t* hash_table_lookup(hash_table_t* ht, void* ptr) {
    if (!ht || !ptr) return NULL;

    pthread_mutex_lock(&ht->lock);
    
    size_t index = hash_ptr(ptr, ht->bucket_count);
    hash_node_t* current = ht->buckets[index];

    while (current) {
        if (current->info.ptr == ptr) {
            pthread_mutex_unlock(&ht->lock);
            return &current->info;
        }
        current = current->next;
    }

    pthread_mutex_unlock(&ht->lock);
    return NULL;
}

bool hash_table_remove(hash_table_t* ht, void* ptr) {
    if (!ht || !ptr) return false;

    pthread_mutex_lock(&ht->lock);

    size_t index = hash_ptr(ptr, ht->bucket_count);
    hash_node_t* current = ht->buckets[index];
    hash_node_t* prev = NULL;

    while (current) {
        if (current->info.ptr == ptr) {
            if (prev) {
                prev->next = current->next;
            } else {
                ht->buckets[index] = current->next;
            }
            free(current);
            ht->item_count--;
            pthread_mutex_unlock(&ht->lock);
            return true;
        }
        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&ht->lock);
    return false;
}

size_t hash_table_count(hash_table_t* ht) {
    if (!ht) return 0;
    pthread_mutex_lock(&ht->lock);
    size_t count = ht->item_count;
    pthread_mutex_unlock(&ht->lock);
    return count;
}
