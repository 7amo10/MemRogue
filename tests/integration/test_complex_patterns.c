/**
 * @file test_complex_patterns.c
 * @brief Integration tests for complex memory allocation patterns
 * 
 * MEMRO-25: Integration Test Suite
 * 
 * Tests complex leak patterns:
 * - Linked list allocations
 * - Binary tree structures
 * - Nested/hierarchical allocations
 * - Circular references
 * - Array of pointers patterns
 */

#include "integration_common.h"
#include "memrogue_tracker.h"

// ============================================================================
// Data Structures
// ============================================================================

typedef struct list_node {
    int value;
    char* data;
    struct list_node* next;
} list_node_t;

typedef struct tree_node {
    int key;
    void* data;
    size_t data_size;
    struct tree_node* left;
    struct tree_node* right;
} tree_node_t;

typedef struct container {
    char* name;
    void** items;
    size_t item_count;
    size_t capacity;
} container_t;

// ============================================================================
// Helper Functions
// ============================================================================

static list_node_t* create_list_node(memory_tracker_t* tracker, int value, const char* data) {
    list_node_t* node = malloc(sizeof(list_node_t));
    if (!node) return NULL;
    
    track_allocation(tracker, node, sizeof(list_node_t), __FILE__, __LINE__);
    
    node->value = value;
    node->next = NULL;
    
    if (data) {
        size_t len = strlen(data) + 1;
        node->data = malloc(len);
        if (node->data) {
            strcpy(node->data, data);
            track_allocation(tracker, node->data, len, __FILE__, __LINE__);
        }
    } else {
        node->data = NULL;
    }
    
    return node;
}

static tree_node_t* create_tree_node(memory_tracker_t* tracker, int key, size_t data_size) {
    tree_node_t* node = malloc(sizeof(tree_node_t));
    if (!node) return NULL;
    
    track_allocation(tracker, node, sizeof(tree_node_t), __FILE__, __LINE__);
    
    node->key = key;
    node->left = NULL;
    node->right = NULL;
    node->data_size = data_size;
    
    if (data_size > 0) {
        node->data = malloc(data_size);
        if (node->data) {
            memset(node->data, key & 0xFF, data_size);
            track_allocation(tracker, node->data, data_size, __FILE__, __LINE__);
        }
    } else {
        node->data = NULL;
    }
    
    return node;
}

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test: Linked list with all nodes leaked
 */
static int test_linked_list_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Create a linked list of 10 nodes
    list_node_t* head = NULL;
    list_node_t* tail = NULL;
    
    for (int i = 0; i < 10; i++) {
        char data[32];
        snprintf(data, sizeof(data), "Node data %d", i);
        
        list_node_t* node = create_list_node(tracker, i, data);
        ASSERT_NOT_NULL(node, "node creation should succeed");
        
        if (!head) {
            head = node;
            tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }
    
    // Verify: 10 nodes * (node struct + data string)
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // 10 list_node_t structures + 10 data strings
    ASSERT_EQ(20, stats.active_allocations, "should have 20 allocations (10 nodes + 10 strings)");
    
    // Save pointers for cleanup
    list_node_t* current = head;
    list_node_t* ptrs[10];
    char* data_ptrs[10];
    int idx = 0;
    while (current && idx < 10) {
        ptrs[idx] = current;
        data_ptrs[idx] = current->data;
        current = current->next;
        idx++;
    }
    
    tracker_destroy(tracker);
    
    // Clean up
    for (int i = 0; i < idx; i++) {
        free(data_ptrs[i]);
        free(ptrs[i]);
    }
    
    return TEST_PASS;
}

/**
 * Test: Partial linked list cleanup (leak in the middle)
 */
static int test_linked_list_partial_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Create 5 nodes
    list_node_t* nodes[5];
    for (int i = 0; i < 5; i++) {
        nodes[i] = create_list_node(tracker, i, NULL);
        ASSERT_NOT_NULL(nodes[i], "node creation should succeed");
        if (i > 0) nodes[i-1]->next = nodes[i];
    }
    
    // Free first 2 and last 2, leak middle one
    track_deallocation(tracker, nodes[0]);
    free(nodes[0]);
    track_deallocation(tracker, nodes[1]);
    free(nodes[1]);
    track_deallocation(tracker, nodes[3]);
    free(nodes[3]);
    track_deallocation(tracker, nodes[4]);
    free(nodes[4]);
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.active_allocations, "should have 1 leaked node");
    ASSERT_EQ(sizeof(list_node_t), stats.active_bytes, "should have node size leaked");
    
    // Save for cleanup
    list_node_t* leaked = nodes[2];
    
    tracker_destroy(tracker);
    free(leaked);
    
    return TEST_PASS;
}

/**
 * Test: Binary tree with all nodes leaked
 */
static int test_binary_tree_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    /* Create a small binary tree:
     *       50
     *      /  \
     *    25    75
     *   /  \  /  \
     *  10  30 60  90
     */
    
    tree_node_t* nodes[7];
    nodes[0] = create_tree_node(tracker, 50, 64);  // root
    nodes[1] = create_tree_node(tracker, 25, 64);
    nodes[2] = create_tree_node(tracker, 75, 64);
    nodes[3] = create_tree_node(tracker, 10, 64);
    nodes[4] = create_tree_node(tracker, 30, 64);
    nodes[5] = create_tree_node(tracker, 60, 64);
    nodes[6] = create_tree_node(tracker, 90, 64);
    
    // Build tree structure
    nodes[0]->left = nodes[1];
    nodes[0]->right = nodes[2];
    nodes[1]->left = nodes[3];
    nodes[1]->right = nodes[4];
    nodes[2]->left = nodes[5];
    nodes[2]->right = nodes[6];
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // 7 nodes + 7 data blocks
    ASSERT_EQ(14, stats.active_allocations, "should have 14 allocations");
    
    // Save data pointers for cleanup
    void* data_ptrs[7];
    for (int i = 0; i < 7; i++) {
        data_ptrs[i] = nodes[i]->data;
    }
    
    tracker_destroy(tracker);
    
    // Clean up
    for (int i = 0; i < 7; i++) {
        free(data_ptrs[i]);
        free(nodes[i]);
    }
    
    return TEST_PASS;
}

/**
 * Test: Deep recursive structure
 */
static int test_deep_nesting(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Create a deeply nested structure (100 levels)
    list_node_t* current = NULL;
    list_node_t* head = NULL;
    list_node_t* all_nodes[100];
    
    for (int i = 0; i < 100; i++) {
        list_node_t* node = malloc(sizeof(list_node_t));
        ASSERT_NOT_NULL(node, "allocation should succeed");
        
        track_allocation(tracker, node, sizeof(list_node_t), __FILE__, __LINE__);
        node->value = i;
        node->data = NULL;
        node->next = NULL;
        
        all_nodes[i] = node;
        
        if (!head) {
            head = node;
        }
        if (current) {
            current->next = node;
        }
        current = node;
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(100, stats.active_allocations, "should have 100 nodes");
    
    tracker_destroy(tracker);
    
    // Clean up
    for (int i = 0; i < 100; i++) {
        free(all_nodes[i]);
    }
    
    return TEST_PASS;
}

/**
 * Test: Array of dynamically allocated strings
 */
static int test_string_array_leak(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const size_t count = 20;
    char** strings = malloc(count * sizeof(char*));
    ASSERT_NOT_NULL(strings, "string array should allocate");
    
    track_allocation(tracker, strings, count * sizeof(char*), __FILE__, __LINE__);
    
    // Allocate each string
    for (size_t i = 0; i < count; i++) {
        char temp[64];
        snprintf(temp, sizeof(temp), "Dynamic string number %zu with some extra text", i);
        
        size_t len = strlen(temp) + 1;
        strings[i] = malloc(len);
        ASSERT_NOT_NULL(strings[i], "string should allocate");
        
        strcpy(strings[i], temp);
        track_allocation(tracker, strings[i], len, __FILE__, __LINE__);
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // 1 array + 20 strings
    ASSERT_EQ(21, stats.active_allocations, "should have 21 allocations");
    
    // Save for cleanup
    char* saved_strings[20];
    for (size_t i = 0; i < count; i++) {
        saved_strings[i] = strings[i];
    }
    char** saved_array = strings;
    
    tracker_destroy(tracker);
    
    // Clean up
    for (size_t i = 0; i < count; i++) {
        free(saved_strings[i]);
    }
    free(saved_array);
    
    return TEST_PASS;
}

/**
 * Test: Container with nested items
 */
static int test_container_pattern(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    // Allocate container
    container_t* container = malloc(sizeof(container_t));
    ASSERT_NOT_NULL(container, "container should allocate");
    track_allocation(tracker, container, sizeof(container_t), __FILE__, __LINE__);
    
    // Allocate name
    const char* name = "TestContainer";
    container->name = malloc(strlen(name) + 1);
    ASSERT_NOT_NULL(container->name, "name should allocate");
    strcpy(container->name, name);
    track_allocation(tracker, container->name, strlen(name) + 1, __FILE__, __LINE__);
    
    // Allocate items array
    container->capacity = 10;
    container->item_count = 5;
    container->items = malloc(container->capacity * sizeof(void*));
    ASSERT_NOT_NULL(container->items, "items should allocate");
    track_allocation(tracker, container->items, container->capacity * sizeof(void*), __FILE__, __LINE__);
    
    // Allocate individual items
    for (size_t i = 0; i < container->item_count; i++) {
        size_t item_size = 32 + i * 16;  // Varying sizes
        container->items[i] = malloc(item_size);
        ASSERT_NOT_NULL(container->items[i], "item should allocate");
        track_allocation(tracker, container->items[i], item_size, __FILE__, __LINE__);
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // container + name + items array + 5 items = 8 allocations
    ASSERT_EQ(8, stats.active_allocations, "should have 8 allocations");
    
    // Save pointers for cleanup
    void* saved_items[5];
    for (size_t i = 0; i < 5; i++) {
        saved_items[i] = container->items[i];
    }
    void* saved_items_array = container->items;
    char* saved_name = container->name;
    container_t* saved_container = container;
    
    tracker_destroy(tracker);
    
    // Clean up (reverse order)
    for (size_t i = 0; i < 5; i++) {
        free(saved_items[i]);
    }
    free(saved_items_array);
    free(saved_name);
    free(saved_container);
    
    return TEST_PASS;
}

/**
 * Test: Matrix (2D array) allocation pattern
 */
static int test_matrix_pattern(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const size_t rows = 10;
    const size_t cols = 10;
    
    /* Allocate row pointers */
    int** matrix = malloc(rows * sizeof(int*));
    ASSERT_NOT_NULL(matrix, "matrix should allocate");
    track_allocation(tracker, matrix, rows * sizeof(int*), __FILE__, __LINE__);
    
    /* Allocate each row */
    for (size_t i = 0; i < rows; i++) {
        matrix[i] = malloc(cols * sizeof(int));
        ASSERT_NOT_NULL(matrix[i], "row should allocate");
        track_allocation(tracker, matrix[i], cols * sizeof(int), __FILE__, __LINE__);
        
        /* Initialize */
        for (size_t j = 0; j < cols; j++) {
            matrix[i][j] = (int)(i * cols + j);
        }
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // 1 row pointer array + 10 row arrays = 11 allocations
    ASSERT_EQ(11, stats.active_allocations, "should have 11 allocations");
    
    // Save for cleanup
    int* saved_rows[10];
    for (size_t i = 0; i < rows; i++) {
        saved_rows[i] = matrix[i];
    }
    int** saved_matrix = matrix;
    
    tracker_destroy(tracker);
    
    // Clean up
    for (size_t i = 0; i < rows; i++) {
        free(saved_rows[i]);
    }
    free(saved_matrix);
    
    return TEST_PASS;
}

/**
 * Test: Reallocating array growth pattern
 */
static int test_growing_array(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    size_t capacity = 4;
    int* array = malloc(capacity * sizeof(int));
    ASSERT_NOT_NULL(array, "initial array should allocate");
    track_allocation(tracker, array, capacity * sizeof(int), __FILE__, __LINE__);
    
    /* Grow the array several times */
    for (int growth = 0; growth < 5; growth++) {
        size_t new_capacity = capacity * 2;
        /*
         * Save old pointer address before realloc for tracking.
         * We use uintptr_t to avoid use-after-free warning since
         * we're only using the value for tracking, not dereferencing.
         */
        uintptr_t old_addr = (uintptr_t)array;
        int* new_array = realloc(array, new_capacity * sizeof(int));
        ASSERT_NOT_NULL(new_array, "realloc should succeed");
        
        /* Track the change using saved address */
        track_deallocation(tracker, (void*)old_addr);
        track_allocation(tracker, new_array, new_capacity * sizeof(int), __FILE__, __LINE__);
        
        array = new_array;
        capacity = new_capacity;
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // After 5 doublings from 4: 4 -> 8 -> 16 -> 32 -> 64 -> 128
    ASSERT_EQ(1, stats.active_allocations, "should have 1 allocation after reallocs");
    ASSERT_EQ(128 * sizeof(int), stats.active_bytes, "should have final size");
    ASSERT_EQ(6, stats.total_allocations, "should have 6 total allocations");
    ASSERT_EQ(5, stats.total_deallocations, "should have 5 deallocations");
    
    int* saved = array;
    tracker_destroy(tracker);
    free(saved);
    
    return TEST_PASS;
}

/**
 * Test: Struct with flexible array member pattern
 */
typedef struct {
    size_t count;
    int data[];  // Flexible array member
} flex_array_t;

static int test_flexible_array(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const size_t count = 50;
    size_t size = sizeof(flex_array_t) + count * sizeof(int);
    
    flex_array_t* fa = malloc(size);
    ASSERT_NOT_NULL(fa, "flex array should allocate");
    track_allocation(tracker, fa, size, __FILE__, __LINE__);
    
    fa->count = count;
    for (size_t i = 0; i < count; i++) {
        fa->data[i] = (int)i * 2;
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    ASSERT_EQ(1, stats.active_allocations, "should have 1 allocation");
    ASSERT_EQ(size, stats.active_bytes, "should track full size");
    
    flex_array_t* saved = fa;
    tracker_destroy(tracker);
    free(saved);
    
    return TEST_PASS;
}

/**
 * Test: Hash map bucket pattern (simulated)
 */
typedef struct hash_entry {
    char* key;
    void* value;
    size_t value_size;
    struct hash_entry* next;
} hash_entry_t;

static int test_hash_bucket_pattern(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const size_t bucket_count = 16;
    hash_entry_t** buckets = calloc(bucket_count, sizeof(hash_entry_t*));
    ASSERT_NOT_NULL(buckets, "buckets should allocate");
    track_allocation(tracker, buckets, bucket_count * sizeof(hash_entry_t*), __FILE__, __LINE__);
    
    // Add entries to different buckets
    const char* keys[] = {"key1", "key2", "key3", "key4"};
    hash_entry_t* saved_entries[4];
    char* saved_keys[4];
    void* saved_values[4];
    
    for (size_t i = 0; i < 4; i++) {
        size_t bucket = i % bucket_count;
        
        hash_entry_t* entry = malloc(sizeof(hash_entry_t));
        ASSERT_NOT_NULL(entry, "entry should allocate");
        track_allocation(tracker, entry, sizeof(hash_entry_t), __FILE__, __LINE__);
        
        entry->key = strdup(keys[i]);
        ASSERT_NOT_NULL(entry->key, "key should allocate");
        track_allocation(tracker, entry->key, strlen(keys[i]) + 1, __FILE__, __LINE__);
        
        entry->value_size = 64;
        entry->value = malloc(entry->value_size);
        ASSERT_NOT_NULL(entry->value, "value should allocate");
        track_allocation(tracker, entry->value, entry->value_size, __FILE__, __LINE__);
        
        entry->next = buckets[bucket];
        buckets[bucket] = entry;
        
        saved_entries[i] = entry;
        saved_keys[i] = entry->key;
        saved_values[i] = entry->value;
    }
    
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    // 1 bucket array + 4 entries * (entry + key + value) = 1 + 12 = 13
    ASSERT_EQ(13, stats.active_allocations, "should have 13 allocations");
    
    hash_entry_t** saved_buckets = buckets;
    
    tracker_destroy(tracker);
    
    // Clean up
    for (int i = 0; i < 4; i++) {
        free(saved_values[i]);
        free(saved_keys[i]);
        free(saved_entries[i]);
    }
    free(saved_buckets);
    
    return TEST_PASS;
}

// ============================================================================
// Test Suite Definition
// ============================================================================

static test_case_t complex_pattern_tests[] = {
    {"linked_list_leak", "Full linked list leaked", test_linked_list_leak},
    {"linked_list_partial_leak", "Linked list with middle node leaked", test_linked_list_partial_leak},
    {"binary_tree_leak", "Complete binary tree leaked", test_binary_tree_leak},
    {"deep_nesting", "100-level deep structure", test_deep_nesting},
    {"string_array_leak", "Array of dynamically allocated strings", test_string_array_leak},
    {"container_pattern", "Container with nested items", test_container_pattern},
    {"matrix_pattern", "2D matrix allocation", test_matrix_pattern},
    {"growing_array", "Realloc-based array growth", test_growing_array},
    {"flexible_array", "Struct with flexible array member", test_flexible_array},
    {"hash_bucket_pattern", "Hash table bucket structure", test_hash_bucket_pattern},
};

int main(void) {
    test_suite_t suite = {
        .name = "Complex Pattern Integration Tests",
        .tests = complex_pattern_tests,
        .test_count = sizeof(complex_pattern_tests) / sizeof(complex_pattern_tests[0]),
    };
    
    int result = run_test_suite(&suite);
    
    return (result == TEST_PASS) ? 0 : 1;
}
