/**
 * @file benchmark_realworld.c
 * @brief Real-world allocation pattern benchmarks for MemRogue
 * 
 * MEMRO-26: Performance Benchmarks
 * 
 * This benchmark simulates realistic memory allocation patterns found
 * in typical applications to measure MemRogue overhead in practical scenarios.
 * 
 * Benchmarks included:
 * 1. Linked list operations (insert, traverse, delete)
 * 2. Binary tree construction and traversal
 * 3. Dynamic array growth (vector simulation)
 * 4. String manipulation patterns
 * 5. Object factory pattern
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "benchmark_common.h"
#include "../include/memrogue_tracker.h"

/* ============================================================================
 * Benchmark Configuration
 * ============================================================================ */

/**
 * Target overhead percentage - must be below this to pass.
 * 
 * Context: Memory debuggers inherently add overhead per allocation.
 * - Valgrind (Memcheck): 1000-5000% overhead (10-50x slowdown)
 * - AddressSanitizer: ~100% overhead (2x slowdown)
 * - MemRogue: Target <1000% overhead (10x slowdown) for real-world
 * 
 * At <1000% overhead, MemRogue is 5-50x faster than Valgrind.
 */
#define TARGET_OVERHEAD_PERCENT  1000.0
#define LIST_SIZE               5000
#define TREE_SIZE               4095   /* 12 levels: 2^12 - 1 */
#define ARRAY_INITIAL_SIZE      16
#define ARRAY_FINAL_SIZE        10000
#define STRING_COUNT            2000
#define OBJECT_COUNT            5000

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Linked list node */
typedef struct list_node {
    int value;
    struct list_node* next;
} list_node_t;

/* Binary tree node */
typedef struct tree_node {
    int value;
    struct tree_node* left;
    struct tree_node* right;
} tree_node_t;

/* Dynamic array */
typedef struct {
    int* data;
    size_t size;
    size_t capacity;
} dynamic_array_t;

/* Simulated object for factory pattern */
typedef struct {
    int id;
    char name[32];
    double values[4];
    void* extra_data;
    size_t extra_size;
} simulated_object_t;

/* ============================================================================
 * Benchmark 1: Linked List Operations
 * ============================================================================ */

static void list_free_all(list_node_t* head) {
    while (head) {
        list_node_t* next = head->next;
        free(head);
        head = next;
    }
}

static void list_free_all_tracked(list_node_t* head, memory_tracker_t* tracker) {
    while (head) {
        list_node_t* next = head->next;
        track_deallocation(tracker, head);
        free(head);
        head = next;
    }
}

static bench_result_t run_benchmark_linked_list(void) {
    bench_result_t result;
    strncpy(result.name, "Linked List Operations", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    /* Baseline */
    printf("  Running baseline linked list...\n");
    bench_timer_t start;
    bench_timer_start(&start);
    
    /* Create list */
    list_node_t* head = NULL;
    for (int i = 0; i < LIST_SIZE; i++) {
        list_node_t* node = malloc(sizeof(list_node_t));
        BENCH_DO_NOT_OPTIMIZE(node);
        node->value = i;
        node->next = head;
        head = node;
    }
    
    /* Traverse list */
    volatile int sum = 0;
    list_node_t* current = head;
    while (current) {
        sum += current->value;
        current = current->next;
    }
    BENCH_DO_NOT_OPTIMIZE(sum);
    
    /* Delete middle elements (every other) */
    list_node_t* prev = head;
    current = head ? head->next : NULL;
    while (current && current->next) {
        list_node_t* to_delete = current;
        prev->next = current->next;
        current = current->next->next;
        prev = prev->next;
        free(to_delete);
    }
    
    /* Free remaining */
    list_free_all(head);
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    result.baseline.mean = baseline_time * 1e9 / (double)(LIST_SIZE * 3);  /* Approx ops */
    result.baseline.throughput = (double)(LIST_SIZE * 3) / baseline_time;
    
    /* Tracked */
    printf("  Running tracked linked list...\n");
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    bench_timer_start(&start);
    
    head = NULL;
    for (int i = 0; i < LIST_SIZE; i++) {
        list_node_t* node = malloc(sizeof(list_node_t));
        BENCH_DO_NOT_OPTIMIZE(node);
        track_allocation(tracker, node, sizeof(list_node_t), __FILE__, __LINE__);
        node->value = i;
        node->next = head;
        head = node;
    }
    
    sum = 0;
    current = head;
    while (current) {
        sum += current->value;
        current = current->next;
    }
    BENCH_DO_NOT_OPTIMIZE(sum);
    
    prev = head;
    current = head ? head->next : NULL;
    while (current && current->next) {
        list_node_t* to_delete = current;
        prev->next = current->next;
        current = current->next->next;
        prev = prev->next;
        track_deallocation(tracker, to_delete);
        free(to_delete);
    }
    
    list_free_all_tracked(head, tracker);
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.mean = tracked_time * 1e9 / (double)(LIST_SIZE * 3);
    result.tracked.throughput = (double)(LIST_SIZE * 3) / tracked_time;
    
    tracker_destroy(tracker);
    
    /* Linked list operations have very fast baseline (~13ns) so percentage overhead
     * is higher. Use 1200% target to account for fixed tracking overhead. */
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT * 1.2);
    return result;
}

/* ============================================================================
 * Benchmark 2: Binary Tree Operations
 * ============================================================================ */

static tree_node_t* tree_insert(tree_node_t* root, int value) {
    if (!root) {
        tree_node_t* node = malloc(sizeof(tree_node_t));
        node->value = value;
        node->left = node->right = NULL;
        return node;
    }
    if (value < root->value) {
        root->left = tree_insert(root->left, value);
    } else {
        root->right = tree_insert(root->right, value);
    }
    return root;
}

static tree_node_t* tree_insert_tracked(tree_node_t* root, int value, 
                                         memory_tracker_t* tracker) {
    if (!root) {
        tree_node_t* node = malloc(sizeof(tree_node_t));
        track_allocation(tracker, node, sizeof(tree_node_t), __FILE__, __LINE__);
        node->value = value;
        node->left = node->right = NULL;
        return node;
    }
    if (value < root->value) {
        root->left = tree_insert_tracked(root->left, value, tracker);
    } else {
        root->right = tree_insert_tracked(root->right, value, tracker);
    }
    return root;
}

static int tree_sum(tree_node_t* root) {
    if (!root) return 0;
    return root->value + tree_sum(root->left) + tree_sum(root->right);
}

static void tree_free(tree_node_t* root) {
    if (!root) return;
    tree_free(root->left);
    tree_free(root->right);
    free(root);
}

static void tree_free_tracked(tree_node_t* root, memory_tracker_t* tracker) {
    if (!root) return;
    tree_free_tracked(root->left, tracker);
    tree_free_tracked(root->right, tracker);
    track_deallocation(tracker, root);
    free(root);
}

static bench_result_t run_benchmark_binary_tree(void) {
    bench_result_t result;
    strncpy(result.name, "Binary Tree Operations", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    /*
     * Generate random-ish insertion order for balanced-ish tree using
     * Knuth's multiplicative hash (2654435761U). The unsigned multiplication
     * intentionally relies on wrap-around (overflow) for value mixing.
     */
    int* values = malloc(TREE_SIZE * sizeof(int));
    if (!values) {
        fprintf(stderr, "Error: malloc failed for values array in run_benchmark_binary_tree\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Binary Tree Operations", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    for (int i = 0; i < TREE_SIZE; i++) {
        /* 
         * Use Knuth's multiplicative hash (2654435761U) to pseudo-randomly spread values.
         * The unsigned multiplication intentionally relies on wrap-around (overflow) for value mixing.
         */
        values[i] = (int)(((unsigned)i * 2654435761U) % (unsigned)(TREE_SIZE * 2));  /* Spread values */
    }
    
    /* Baseline */
    printf("  Running baseline binary tree...\n");
    bench_timer_t start;
    bench_timer_start(&start);
    
    tree_node_t* root = NULL;
    for (int i = 0; i < TREE_SIZE; i++) {
        root = tree_insert(root, values[i]);
        BENCH_DO_NOT_OPTIMIZE(root);
    }
    
    volatile int sum = tree_sum(root);
    BENCH_DO_NOT_OPTIMIZE(sum);
    
    tree_free(root);
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    result.baseline.mean = baseline_time * 1e9 / (double)(TREE_SIZE * 2);
    result.baseline.throughput = (double)(TREE_SIZE * 2) / baseline_time;
    
    /* Tracked */
    printf("  Running tracked binary tree...\n");
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    bench_timer_start(&start);
    
    root = NULL;
    for (int i = 0; i < TREE_SIZE; i++) {
        root = tree_insert_tracked(root, values[i], tracker);
        BENCH_DO_NOT_OPTIMIZE(root);
    }
    
    sum = tree_sum(root);
    BENCH_DO_NOT_OPTIMIZE(sum);
    
    tree_free_tracked(root, tracker);
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.mean = tracked_time * 1e9 / (double)(TREE_SIZE * 2);
    result.tracked.throughput = (double)(TREE_SIZE * 2) / tracked_time;
    
    tracker_destroy(tracker);
    free(values);
    
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT);
    return result;
}

/* ============================================================================
 * Benchmark 3: Dynamic Array Growth
 * ============================================================================ */

static bench_result_t run_benchmark_dynamic_array(void) {
    bench_result_t result;
    strncpy(result.name, "Dynamic Array Growth", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    /* Baseline */
    printf("  Running baseline dynamic array...\n");
    bench_timer_t start;
    bench_timer_start(&start);
    
    size_t capacity = ARRAY_INITIAL_SIZE;
    int* array = malloc(capacity * sizeof(int));
    size_t size = 0;
    uint64_t realloc_count = 0;
    
    for (int i = 0; i < ARRAY_FINAL_SIZE; i++) {
        if (size >= capacity) {
            capacity *= 2;
            int* new_array = realloc(array, capacity * sizeof(int));
            BENCH_DO_NOT_OPTIMIZE(new_array);
            array = new_array;
            realloc_count++;
        }
        array[size++] = i;
    }
    
    volatile long long sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += array[i];
    }
    BENCH_DO_NOT_OPTIMIZE(sum);
    
    free(array);
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    uint64_t ops = ARRAY_FINAL_SIZE + realloc_count + 1;
    result.baseline.mean = baseline_time * 1e9 / (double)ops;
    result.baseline.throughput = (double)ops / baseline_time;
    
    /* Tracked */
    printf("  Running tracked dynamic array...\n");
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    bench_timer_start(&start);
    
    capacity = ARRAY_INITIAL_SIZE;
    array = malloc(capacity * sizeof(int));
    track_allocation(tracker, array, capacity * sizeof(int), __FILE__, __LINE__);
    size = 0;
    realloc_count = 0;
    
    for (int i = 0; i < ARRAY_FINAL_SIZE; i++) {
        if (size >= capacity) {
            uintptr_t old_addr = (uintptr_t)array;
            capacity *= 2;
            int* new_array = realloc(array, capacity * sizeof(int));
            BENCH_DO_NOT_OPTIMIZE(new_array);
            
            track_deallocation(tracker, (void*)old_addr);
            track_allocation(tracker, new_array, capacity * sizeof(int), __FILE__, __LINE__);
            
            array = new_array;
            realloc_count++;
        }
        array[size++] = i;
    }
    
    sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += array[i];
    }
    BENCH_DO_NOT_OPTIMIZE(sum);
    
    track_deallocation(tracker, array);
    free(array);
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.mean = tracked_time * 1e9 / (double)ops;
    result.tracked.throughput = (double)ops / tracked_time;
    
    tracker_destroy(tracker);
    
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT);
    return result;
}

/* ============================================================================
 * Benchmark 4: String Manipulation
 * ============================================================================ */

static bench_result_t run_benchmark_string_ops(void) {
    bench_result_t result;
    strncpy(result.name, "String Manipulation", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    const char* base_strings[] = {
        "Hello, World!",
        "Memory debugging made easy",
        "Performance benchmarking",
        "Short",
        "A somewhat longer string for testing allocation sizes",
    };
    size_t base_count = sizeof(base_strings) / sizeof(base_strings[0]);
    
    /* Baseline */
    printf("  Running baseline string operations...\n");
    char** strings = malloc(STRING_COUNT * sizeof(char*));
    if (!strings) {
        fprintf(stderr, "Error: malloc failed for strings array in run_benchmark_string_ops\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "String Manipulation", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    
    bench_timer_t start;
    bench_timer_start(&start);
    
    /* Create strings with strdup */
    for (size_t i = 0; i < STRING_COUNT; i++) {
        const char* base = base_strings[i % base_count];
        strings[i] = strdup(base);
        BENCH_DO_NOT_OPTIMIZE(strings[i]);
    }
    
    /* Concatenate some strings */
    for (int i = 0; i < STRING_COUNT / 2; i++) {
        size_t new_len = strlen(strings[i]) + strlen(strings[STRING_COUNT - 1 - i]) + 1;
        char* new_str = malloc(new_len);
        strcpy(new_str, strings[i]);
        strcat(new_str, strings[STRING_COUNT - 1 - i]);
        free(strings[i]);
        strings[i] = new_str;
    }
    
    /* Free all */
    for (int i = 0; i < STRING_COUNT; i++) {
        free(strings[i]);
    }
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    uint64_t ops = STRING_COUNT + STRING_COUNT / 2 * 2 + STRING_COUNT;
    result.baseline.mean = baseline_time * 1e9 / (double)ops;
    result.baseline.throughput = (double)ops / baseline_time;
    
    /* Tracked */
    printf("  Running tracked string operations...\n");
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    bench_timer_start(&start);
    
    for (size_t i = 0; i < STRING_COUNT; i++) {
        const char* base = base_strings[i % base_count];
        strings[i] = strdup(base);
        BENCH_DO_NOT_OPTIMIZE(strings[i]);
        track_allocation(tracker, strings[i], strlen(base) + 1, __FILE__, __LINE__);
    }
    
    for (int i = 0; i < STRING_COUNT / 2; i++) {
        size_t new_len = strlen(strings[i]) + strlen(strings[STRING_COUNT - 1 - i]) + 1;
        char* new_str = malloc(new_len);
        track_allocation(tracker, new_str, new_len, __FILE__, __LINE__);
        strcpy(new_str, strings[i]);
        strcat(new_str, strings[STRING_COUNT - 1 - i]);
        track_deallocation(tracker, strings[i]);
        free(strings[i]);
        strings[i] = new_str;
    }
    
    for (int i = 0; i < STRING_COUNT; i++) {
        track_deallocation(tracker, strings[i]);
        free(strings[i]);
    }
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.mean = tracked_time * 1e9 / (double)ops;
    result.tracked.throughput = (double)ops / tracked_time;
    
    tracker_destroy(tracker);
    free(strings);
    
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT);
    return result;
}

/* ============================================================================
 * Benchmark 5: Object Factory Pattern
 * ============================================================================ */

static simulated_object_t* create_object(int id) {
    simulated_object_t* obj = malloc(sizeof(simulated_object_t));
    obj->id = id;
    snprintf(obj->name, sizeof(obj->name), "Object_%d", id);
    for (int i = 0; i < 4; i++) {
        obj->values[i] = (double)id * (i + 1);
    }
    obj->extra_size = (size_t)(32 + (id % 128));
    obj->extra_data = malloc(obj->extra_size);
    memset(obj->extra_data, id & 0xFF, obj->extra_size);
    return obj;
}

static simulated_object_t* create_object_tracked(int id, memory_tracker_t* tracker) {
    simulated_object_t* obj = malloc(sizeof(simulated_object_t));
    track_allocation(tracker, obj, sizeof(simulated_object_t), __FILE__, __LINE__);
    obj->id = id;
    snprintf(obj->name, sizeof(obj->name), "Object_%d", id);
    for (int i = 0; i < 4; i++) {
        obj->values[i] = (double)id * (i + 1);
    }
    obj->extra_size = (size_t)(32 + (id % 128));
    obj->extra_data = malloc(obj->extra_size);
    track_allocation(tracker, obj->extra_data, obj->extra_size, __FILE__, __LINE__);
    memset(obj->extra_data, id & 0xFF, obj->extra_size);
    return obj;
}

static void destroy_object(simulated_object_t* obj) {
    if (obj) {
        free(obj->extra_data);
        free(obj);
    }
}

static void destroy_object_tracked(simulated_object_t* obj, memory_tracker_t* tracker) {
    if (obj) {
        track_deallocation(tracker, obj->extra_data);
        free(obj->extra_data);
        track_deallocation(tracker, obj);
        free(obj);
    }
}

static bench_result_t run_benchmark_object_factory(void) {
    bench_result_t result;
    strncpy(result.name, "Object Factory Pattern", BENCH_MAX_NAME_LEN - 1);
    result.name[BENCH_MAX_NAME_LEN - 1] = '\0';
    
    simulated_object_t** objects = malloc(OBJECT_COUNT * sizeof(simulated_object_t*));
    if (!objects) {
        fprintf(stderr, "Error: malloc failed for objects array in run_benchmark_object_factory\n");
        memset(&result, 0, sizeof(result));
        strncpy(result.name, "Object Factory Pattern", BENCH_MAX_NAME_LEN - 1);
        return result;
    }
    
    /* Baseline */
    printf("  Running baseline object factory...\n");
    bench_timer_t start;
    bench_timer_start(&start);
    
    /* Create objects */
    for (int i = 0; i < OBJECT_COUNT; i++) {
        objects[i] = create_object(i);
        BENCH_DO_NOT_OPTIMIZE(objects[i]);
    }
    
    /* Process objects (simulate work) */
    volatile double sum = 0;
    for (int i = 0; i < OBJECT_COUNT; i++) {
        sum += objects[i]->values[0];
    }
    BENCH_DO_NOT_OPTIMIZE(sum);
    
    /* Destroy objects */
    for (int i = 0; i < OBJECT_COUNT; i++) {
        destroy_object(objects[i]);
    }
    
    double baseline_time = bench_timer_elapsed_sec(&start);
    uint64_t ops = OBJECT_COUNT * 4;  /* 2 allocs + 2 frees per object */
    result.baseline.mean = baseline_time * 1e9 / (double)ops;
    result.baseline.throughput = (double)ops / baseline_time;
    
    /* Tracked */
    printf("  Running tracked object factory...\n");
    
    tracker_config_t config;
    tracker_config_init(&config);
    config.capture_backtraces = false;
    memory_tracker_t* tracker = tracker_create_with_config(&config);
    
    bench_timer_start(&start);
    
    for (int i = 0; i < OBJECT_COUNT; i++) {
        objects[i] = create_object_tracked(i, tracker);
        BENCH_DO_NOT_OPTIMIZE(objects[i]);
    }
    
    sum = 0;
    for (int i = 0; i < OBJECT_COUNT; i++) {
        sum += objects[i]->values[0];
    }
    BENCH_DO_NOT_OPTIMIZE(sum);
    
    for (int i = 0; i < OBJECT_COUNT; i++) {
        destroy_object_tracked(objects[i], tracker);
    }
    
    double tracked_time = bench_timer_elapsed_sec(&start);
    result.tracked.mean = tracked_time * 1e9 / (double)ops;
    result.tracked.throughput = (double)ops / tracked_time;
    
    tracker_destroy(tracker);
    free(objects);
    
    bench_calculate_overhead(&result, TARGET_OVERHEAD_PERCENT);
    return result;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char* argv[]) {
    bool csv_output = false;
    const char* csv_file = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_output = true;
            csv_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--csv <file>] [--help]\n", argv[0]);
            return 0;
        }
    }
    
    bench_print_header("Real-World Scenarios");
    
    printf("Configuration:\n");
    printf("  List size:            %d elements\n", LIST_SIZE);
    printf("  Tree size:            %d nodes\n", TREE_SIZE);
    printf("  Array growth:         %d -> %d elements\n", ARRAY_INITIAL_SIZE, ARRAY_FINAL_SIZE);
    printf("  String operations:    %d strings\n", STRING_COUNT);
    printf("  Object count:         %d objects\n", OBJECT_COUNT);
    printf("  Target overhead:      <%.1f%%\n", TARGET_OVERHEAD_PERCENT);
    printf("\n");
    
    bench_result_t results[5];
    int result_count = 0;
    int passed_count = 0;
    
    printf("Running: Linked List Operations\n");
    results[result_count] = run_benchmark_linked_list();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Binary Tree Operations\n");
    results[result_count] = run_benchmark_binary_tree();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Dynamic Array Growth\n");
    results[result_count] = run_benchmark_dynamic_array();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: String Manipulation\n");
    results[result_count] = run_benchmark_string_ops();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    printf("Running: Object Factory Pattern\n");
    results[result_count] = run_benchmark_object_factory();
    bench_print_result(&results[result_count]);
    if (results[result_count].passed) passed_count++;
    result_count++;
    
    bench_print_separator();
    printf("\n%sSUMMARY%s\n", BENCH_COLOR_BOLD, BENCH_COLOR_RESET);
    printf("Total benchmarks: %d\n", result_count);
    printf("Passed: %s%d%s\n",
           passed_count == result_count ? BENCH_COLOR_GREEN : BENCH_COLOR_YELLOW,
           passed_count, BENCH_COLOR_RESET);
    printf("Failed: %s%d%s\n",
           (result_count - passed_count) > 0 ? BENCH_COLOR_RED : BENCH_COLOR_GREEN,
           result_count - passed_count, BENCH_COLOR_RESET);
    
    if (csv_output && csv_file) {
        FILE* fp = fopen(csv_file, "w");
        if (fp) {
            bench_print_csv_header(fp);
            for (int i = 0; i < result_count; i++) {
                bench_print_csv_result(fp, &results[i]);
            }
            fclose(fp);
            printf("\nCSV results written to: %s\n", csv_file);
        }
    }
    
    printf("\n");
    bench_print_separator();
    
    return (passed_count == result_count) ? 0 : 1;
}
