/**
 * @file test_multithreaded_integration.c
 * @brief Integration tests for multithreaded memory allocation scenarios
 * 
 * MEMRO-25: Integration Test Suite
 * 
 * Tests thread safety and concurrent allocation tracking:
 * - Concurrent allocations from multiple threads
 * - Producer-consumer patterns
 * - Race condition stress testing
 * - Thread-local allocation patterns
 * 
 * IMPORTANT: All tests are carefully designed to avoid:
 * - Deadlocks (proper lock ordering, no nested locks in tests)
 * - Race conditions (using atomic operations where needed)
 * - Memory leaks (comprehensive cleanup)
 */

#include "integration_common.h"
#include "memrogue_tracker.h"

#include <pthread.h>
#include <stdatomic.h>

// ============================================================================
// Thread-Safe Counters
// ============================================================================

typedef struct {
    atomic_uint_least64_t allocations;
    atomic_uint_least64_t deallocations;
    atomic_uint_least64_t errors;
} thread_stats_t;

// ============================================================================
// Test Context Structures
// ============================================================================

typedef struct {
    memory_tracker_t* tracker;
    int thread_id;
    int num_allocations;
    size_t allocation_size;
    thread_stats_t* stats;
    void** allocated_ptrs;  // For cleanup
    pthread_mutex_t* ptr_mutex;  // Protect allocated_ptrs access
    int* ptr_count;
} thread_context_t;

typedef struct {
    memory_tracker_t* tracker;
    void** queue;
    int queue_size;
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    atomic_bool done;
    thread_stats_t* stats;
} queue_context_t;

// ============================================================================
// Thread Functions
// ============================================================================

/**
 * Thread function: allocate and track multiple blocks
 */
static void* allocate_thread(void* arg) {
    thread_context_t* ctx = (thread_context_t*)arg;
    
    for (int i = 0; i < ctx->num_allocations; i++) {
        void* ptr = malloc(ctx->allocation_size);
        if (ptr) {
            if (track_allocation(ctx->tracker, ptr, ctx->allocation_size, __FILE__, __LINE__)) {
                atomic_fetch_add(&ctx->stats->allocations, 1);
                
                // Store for cleanup
                pthread_mutex_lock(ctx->ptr_mutex);
                if (*ctx->ptr_count < ctx->num_allocations * 10) {  // Safety bound
                    ctx->allocated_ptrs[*ctx->ptr_count] = ptr;
                    (*ctx->ptr_count)++;
                }
                pthread_mutex_unlock(ctx->ptr_mutex);
            } else {
                atomic_fetch_add(&ctx->stats->errors, 1);
                free(ptr);
            }
        } else {
            atomic_fetch_add(&ctx->stats->errors, 1);
        }
    }
    
    return NULL;
}

/**
 * Thread function: allocate, use briefly, then free
 */
static void* allocate_and_free_thread(void* arg) {
    thread_context_t* ctx = (thread_context_t*)arg;
    
    for (int i = 0; i < ctx->num_allocations; i++) {
        void* ptr = malloc(ctx->allocation_size);
        if (ptr) {
            if (track_allocation(ctx->tracker, ptr, ctx->allocation_size, __FILE__, __LINE__)) {
                atomic_fetch_add(&ctx->stats->allocations, 1);
                
                // Brief use
                memset(ptr, ctx->thread_id, ctx->allocation_size);
                
                // Free
                track_deallocation(ctx->tracker, ptr);
                free(ptr);
                atomic_fetch_add(&ctx->stats->deallocations, 1);
            } else {
                free(ptr);
                atomic_fetch_add(&ctx->stats->errors, 1);
            }
        }
    }
    
    return NULL;
}

/**
 * Producer thread: allocate and add to queue
 */
static void* producer_thread(void* arg) {
    queue_context_t* ctx = (queue_context_t*)arg;
    int produced = 0;
    
    while (produced < 100) {
        void* ptr = malloc(64);
        if (!ptr) {
            atomic_fetch_add(&ctx->stats->errors, 1);
            continue;
        }
        
        if (!track_allocation(ctx->tracker, ptr, 64, __FILE__, __LINE__)) {
            free(ptr);
            atomic_fetch_add(&ctx->stats->errors, 1);
            continue;
        }
        
        atomic_fetch_add(&ctx->stats->allocations, 1);
        
        pthread_mutex_lock(&ctx->mutex);
        while (ctx->count >= ctx->queue_size) {
            pthread_cond_wait(&ctx->not_full, &ctx->mutex);
        }
        
        ctx->queue[ctx->tail] = ptr;
        ctx->tail = (ctx->tail + 1) % ctx->queue_size;
        ctx->count++;
        produced++;
        
        pthread_cond_signal(&ctx->not_empty);
        pthread_mutex_unlock(&ctx->mutex);
    }
    
    return NULL;
}

/**
 * Consumer thread: take from queue and free
 */
static void* consumer_thread(void* arg) {
    queue_context_t* ctx = (queue_context_t*)arg;
    int consumed = 0;
    
    while (consumed < 100 || !atomic_load(&ctx->done)) {
        pthread_mutex_lock(&ctx->mutex);
        
        while (ctx->count == 0 && !atomic_load(&ctx->done)) {
            pthread_cond_wait(&ctx->not_empty, &ctx->mutex);
        }
        
        if (ctx->count == 0) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }
        
        void* ptr = ctx->queue[ctx->head];
        ctx->head = (ctx->head + 1) % ctx->queue_size;
        ctx->count--;
        consumed++;
        
        pthread_cond_signal(&ctx->not_full);
        pthread_mutex_unlock(&ctx->mutex);
        
        // Free outside of lock
        track_deallocation(ctx->tracker, ptr);
        free(ptr);
        atomic_fetch_add(&ctx->stats->deallocations, 1);
    }
    
    return NULL;
}

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test: Multiple threads allocating concurrently
 */
static int test_concurrent_allocations(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const int num_threads = 4;
    const int allocs_per_thread = 100;
    const size_t alloc_size = 64;
    
    thread_stats_t stats = {0};
    pthread_t threads[4];
    thread_context_t contexts[4];
    
    // Shared storage for cleanup
    void* all_ptrs[400];
    int ptr_count = 0;
    pthread_mutex_t ptr_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    // Start threads
    for (int i = 0; i < num_threads; i++) {
        contexts[i] = (thread_context_t){
            .tracker = tracker,
            .thread_id = i,
            .num_allocations = allocs_per_thread,
            .allocation_size = alloc_size,
            .stats = &stats,
            .allocated_ptrs = all_ptrs,
            .ptr_mutex = &ptr_mutex,
            .ptr_count = &ptr_count
        };
        
        int rc = pthread_create(&threads[i], NULL, allocate_thread, &contexts[i]);
        ASSERT_EQ(0, rc, "thread creation should succeed");
    }
    
    // Wait for all threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Verify stats
    tracker_stats_t tracker_stats;
    tracker_get_stats(tracker, &tracker_stats);
    
    uint64_t expected = (uint64_t)num_threads * (uint64_t)allocs_per_thread;
    ASSERT_EQ(expected, atomic_load(&stats.allocations), "should have all allocations");
    ASSERT_EQ(expected, tracker_stats.active_allocations, "tracker should show all as active");
    ASSERT_EQ(0, atomic_load(&stats.errors), "should have no errors");
    
    tracker_destroy(tracker);
    
    // Clean up
    for (int i = 0; i < ptr_count; i++) {
        free(all_ptrs[i]);
    }
    pthread_mutex_destroy(&ptr_mutex);
    
    return TEST_PASS;
}

/**
 * Test: Threads allocating and freeing immediately
 */
static int test_rapid_alloc_free(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const int num_threads = 4;
    const int allocs_per_thread = 500;
    
    thread_stats_t stats = {0};
    pthread_t threads[4];
    thread_context_t contexts[4];
    
    for (int i = 0; i < num_threads; i++) {
        contexts[i] = (thread_context_t){
            .tracker = tracker,
            .thread_id = i,
            .num_allocations = allocs_per_thread,
            .allocation_size = (size_t)(32 + i * 16),  // Varying sizes
            .stats = &stats,
            .allocated_ptrs = NULL,
            .ptr_mutex = NULL,
            .ptr_count = NULL
        };
        
        int rc = pthread_create(&threads[i], NULL, allocate_and_free_thread, &contexts[i]);
        ASSERT_EQ(0, rc, "thread creation should succeed");
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    tracker_stats_t tracker_stats;
    tracker_get_stats(tracker, &tracker_stats);
    
    uint64_t expected = (uint64_t)num_threads * (uint64_t)allocs_per_thread;
    ASSERT_EQ(expected, atomic_load(&stats.allocations), "should have all allocations");
    ASSERT_EQ(expected, atomic_load(&stats.deallocations), "should have all deallocations");
    ASSERT_EQ(0, tracker_stats.active_allocations, "should have no active allocations");
    ASSERT_EQ(0, atomic_load(&stats.errors), "should have no errors");
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Producer-consumer pattern
 */
static int test_producer_consumer(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    thread_stats_t stats = {0};
    void* queue_buffer[16];
    
    queue_context_t queue = {
        .tracker = tracker,
        .queue = queue_buffer,
        .queue_size = 16,
        .head = 0,
        .tail = 0,
        .count = 0,
        .done = false,
        .stats = &stats
    };
    
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.not_empty, NULL);
    pthread_cond_init(&queue.not_full, NULL);
    
    pthread_t producer, consumer;
    
    int rc = pthread_create(&producer, NULL, producer_thread, &queue);
    ASSERT_EQ(0, rc, "producer creation should succeed");
    
    rc = pthread_create(&consumer, NULL, consumer_thread, &queue);
    ASSERT_EQ(0, rc, "consumer creation should succeed");
    
    pthread_join(producer, NULL);
    
    // Signal done and wake consumer
    atomic_store(&queue.done, true);
    pthread_cond_broadcast(&queue.not_empty);
    
    pthread_join(consumer, NULL);
    
    tracker_stats_t tracker_stats;
    tracker_get_stats(tracker, &tracker_stats);
    
    ASSERT_EQ(100, atomic_load(&stats.allocations), "should produce 100 items");
    ASSERT_EQ(100, atomic_load(&stats.deallocations), "should consume 100 items");
    ASSERT_EQ(0, tracker_stats.active_allocations, "should have no leaks");
    
    pthread_mutex_destroy(&queue.mutex);
    pthread_cond_destroy(&queue.not_empty);
    pthread_cond_destroy(&queue.not_full);
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: High contention stress test
 */
static int test_high_contention(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const int num_threads = 8;
    const int allocs_per_thread = 200;
    
    thread_stats_t stats = {0};
    pthread_t threads[8];
    thread_context_t contexts[8];
    
    for (int i = 0; i < num_threads; i++) {
        contexts[i] = (thread_context_t){
            .tracker = tracker,
            .thread_id = i,
            .num_allocations = allocs_per_thread,
            .allocation_size = 16,  // Small, fast allocations
            .stats = &stats,
            .allocated_ptrs = NULL,
            .ptr_mutex = NULL,
            .ptr_count = NULL
        };
        
        int rc = pthread_create(&threads[i], NULL, allocate_and_free_thread, &contexts[i]);
        ASSERT_EQ(0, rc, "thread creation should succeed");
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    tracker_stats_t tracker_stats;
    tracker_get_stats(tracker, &tracker_stats);
    
    uint64_t expected = (uint64_t)num_threads * (uint64_t)allocs_per_thread;
    ASSERT_EQ(expected, tracker_stats.total_allocations, "should track all allocations");
    ASSERT_EQ(expected, tracker_stats.total_deallocations, "should track all deallocations");
    ASSERT_EQ(0, tracker_stats.active_allocations, "should have no leaks");
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Mixed allocation sizes under contention
 */
static int test_mixed_sizes_concurrent(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const int num_threads = 4;
    thread_stats_t stats = {0};
    pthread_t threads[4];
    thread_context_t contexts[4];
    
    // Each thread uses different allocation sizes
    size_t sizes[] = {8, 64, 256, 1024};
    
    for (int i = 0; i < num_threads; i++) {
        contexts[i] = (thread_context_t){
            .tracker = tracker,
            .thread_id = i,
            .num_allocations = 100,
            .allocation_size = sizes[i],
            .stats = &stats,
            .allocated_ptrs = NULL,
            .ptr_mutex = NULL,
            .ptr_count = NULL
        };
        
        int rc = pthread_create(&threads[i], NULL, allocate_and_free_thread, &contexts[i]);
        ASSERT_EQ(0, rc, "thread creation should succeed");
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    tracker_stats_t tracker_stats;
    tracker_get_stats(tracker, &tracker_stats);
    
    ASSERT_EQ(400, tracker_stats.total_allocations, "should track 400 allocations");
    ASSERT_EQ(0, tracker_stats.active_allocations, "should have no leaks");
    
    tracker_destroy(tracker);
    
    return TEST_PASS;
}

/**
 * Test: Thread-local allocations with shared tracker
 */
static int test_thread_local_pattern(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    const int num_threads = 4;
    thread_stats_t stats = {0};
    pthread_t threads[4];
    thread_context_t contexts[4];
    
    void* all_ptrs[400];
    int ptr_count = 0;
    pthread_mutex_t ptr_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    // Each thread allocates its own batch
    for (int i = 0; i < num_threads; i++) {
        contexts[i] = (thread_context_t){
            .tracker = tracker,
            .thread_id = i,
            .num_allocations = 50,
            .allocation_size = 128,
            .stats = &stats,
            .allocated_ptrs = all_ptrs,
            .ptr_mutex = &ptr_mutex,
            .ptr_count = &ptr_count
        };
        
        int rc = pthread_create(&threads[i], NULL, allocate_thread, &contexts[i]);
        ASSERT_EQ(0, rc, "thread creation should succeed");
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    tracker_stats_t tracker_stats;
    tracker_get_stats(tracker, &tracker_stats);
    
    // All 200 allocations should be tracked as active (leaked)
    ASSERT_EQ(200, tracker_stats.active_allocations, "should have 200 active allocations");
    ASSERT_EQ(200 * 128, tracker_stats.active_bytes, "should have correct bytes");
    
    tracker_destroy(tracker);
    
    // Clean up
    for (int i = 0; i < ptr_count; i++) {
        free(all_ptrs[i]);
    }
    pthread_mutex_destroy(&ptr_mutex);
    
    return TEST_PASS;
}

/**
 * Test: Sequential thread handoff (no concurrent access)
 */
static int test_sequential_threads(void) {
    memory_tracker_t* tracker = tracker_create();
    ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
    
    void* all_ptrs[200];
    int ptr_count = 0;
    pthread_mutex_t ptr_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    // Run threads one at a time
    for (int t = 0; t < 4; t++) {
        thread_stats_t stats = {0};
        thread_context_t ctx = {
            .tracker = tracker,
            .thread_id = t,
            .num_allocations = 50,
            .allocation_size = 64,
            .stats = &stats,
            .allocated_ptrs = all_ptrs,
            .ptr_mutex = &ptr_mutex,
            .ptr_count = &ptr_count
        };
        
        pthread_t thread;
        int rc = pthread_create(&thread, NULL, allocate_thread, &ctx);
        ASSERT_EQ(0, rc, "thread creation should succeed");
        
        pthread_join(thread, NULL);
        
        ASSERT_EQ(50, atomic_load(&stats.allocations), "thread should allocate 50");
    }
    
    tracker_stats_t tracker_stats;
    tracker_get_stats(tracker, &tracker_stats);
    
    ASSERT_EQ(200, tracker_stats.active_allocations, "should have 200 total");
    
    tracker_destroy(tracker);
    
    for (int i = 0; i < ptr_count; i++) {
        free(all_ptrs[i]);
    }
    pthread_mutex_destroy(&ptr_mutex);
    
    return TEST_PASS;
}

/**
 * Test: Verify no deadlock with rapid create/destroy
 */
static int test_no_deadlock_rapid_ops(void) {
    const int iterations = 100;
    
    for (int i = 0; i < iterations; i++) {
        memory_tracker_t* tracker = tracker_create();
        ASSERT_NOT_NULL(tracker, "tracker creation should succeed");
        
        void* ptr = malloc(64);
        if (ptr) {
            track_allocation(tracker, ptr, 64, __FILE__, __LINE__);
            track_deallocation(tracker, ptr);
            free(ptr);
        }
        
        tracker_destroy(tracker);
    }
    
    return TEST_PASS;
}

// ============================================================================
// Test Suite Definition
// ============================================================================

static test_case_t multithreaded_tests[] = {
    {"concurrent_allocations", "Multiple threads allocating concurrently", test_concurrent_allocations},
    {"rapid_alloc_free", "Threads allocating and freeing rapidly", test_rapid_alloc_free},
    {"producer_consumer", "Producer-consumer queue pattern", test_producer_consumer},
    {"high_contention", "High contention stress test (8 threads)", test_high_contention},
    {"mixed_sizes_concurrent", "Mixed allocation sizes under contention", test_mixed_sizes_concurrent},
    {"thread_local_pattern", "Thread-local allocations with shared tracker", test_thread_local_pattern},
    {"sequential_threads", "Sequential thread handoff", test_sequential_threads},
    {"no_deadlock_rapid_ops", "Rapid create/destroy operations", test_no_deadlock_rapid_ops},
};

int main(void) {
    test_suite_t suite = {
        .name = "Multithreaded Integration Tests",
        .tests = multithreaded_tests,
        .test_count = sizeof(multithreaded_tests) / sizeof(multithreaded_tests[0]),
    };
    
    int result = run_test_suite(&suite);
    
    return (result == TEST_PASS) ? 0 : 1;
}
