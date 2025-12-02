/**
 * @file multithreaded.c
 * @brief Multithreaded memory allocation example for MemRogue demonstration
 * 
 * MEMRO-27: Example Applications
 * 
 * This example demonstrates memory allocation patterns in a multithreaded
 * environment. It showcases:
 * - Concurrent allocations from multiple threads
 * - Thread-local allocation patterns
 * - Producer-consumer memory patterns
 * - Intentional race condition leak (for demonstration)
 * 
 * The example is designed to be thread-safe (no data races on shared data)
 * while still demonstrating memory leak scenarios.
 * 
 * Usage:
 *   # Build with CMake
 *   cd build && make multithreaded_example
 *   
 *   # Run with MemRogue interception
 *   LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/multithreaded_example
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define NUM_WORKER_THREADS    4
#define ALLOCATIONS_PER_THREAD 10
#define WORK_ITEM_SIZE        128
#define QUEUE_SIZE            16

/* ============================================================================
 * Thread-Safe Work Queue
 * ============================================================================
 * A simple bounded queue for producer-consumer pattern.
 * Uses mutex and condition variables for synchronization.
 */

typedef struct {
    void* items[QUEUE_SIZE];
    size_t sizes[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    bool shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} work_queue_t;

static void queue_init(work_queue_t* q) {
    memset(q->items, 0, sizeof(q->items));
    memset(q->sizes, 0, sizeof(q->sizes));
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->shutdown = false;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void queue_destroy(work_queue_t* q) {
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

static bool queue_push(work_queue_t* q, void* item, size_t size) {
    pthread_mutex_lock(&q->mutex);
    
    /* Wait until queue is not full or shutdown */
    while (q->count == QUEUE_SIZE && !q->shutdown) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    
    if (q->shutdown) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }
    
    q->items[q->tail] = item;
    q->sizes[q->tail] = size;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

static bool queue_pop(work_queue_t* q, void** item, size_t* size) {
    pthread_mutex_lock(&q->mutex);
    
    /* Wait until queue is not empty or shutdown */
    while (q->count == 0 && !q->shutdown) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    
    if (q->count == 0 && q->shutdown) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }
    
    *item = q->items[q->head];
    *size = q->sizes[q->head];
    q->items[q->head] = NULL;
    q->sizes[q->head] = 0;
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

static void queue_shutdown(work_queue_t* q) {
    pthread_mutex_lock(&q->mutex);
    q->shutdown = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

/* ============================================================================
 * Statistics Tracking (Thread-Safe)
 * ============================================================================ */

typedef struct {
    atomic_size_t total_allocated;
    atomic_size_t total_freed;
    atomic_size_t allocation_count;
    atomic_size_t free_count;
    atomic_size_t intentional_leaks;
} stats_t;

static stats_t g_stats = {0};

/* ============================================================================
 * Example 1: Thread-Local Allocation Pattern
 * ============================================================================
 * Each thread allocates and frees its own memory without sharing.
 * This is the safest pattern for multithreaded memory management.
 */

typedef struct {
    int thread_id;
    int allocations;
    bool leak_some;  /* Intentionally leak some allocations */
} worker_args_t;

static void* thread_local_worker(void* arg) {
    worker_args_t* args = (worker_args_t*)arg;
    
    printf("[Thread %d] Starting thread-local allocations\n", args->thread_id);
    
    for (int i = 0; i < args->allocations; i++) {
        /* Allocate thread-local buffer */
        size_t size = 64 + (size_t)(args->thread_id * 16);
        char* buffer = malloc(size);
        if (!buffer) {
            fprintf(stderr, "[Thread %d] malloc failed\n", args->thread_id);
            continue;
        }
        
        atomic_fetch_add(&g_stats.total_allocated, size);
        atomic_fetch_add(&g_stats.allocation_count, 1);
        
        /* Use the buffer */
        snprintf(buffer, size, "Thread %d, allocation %d", 
                 args->thread_id, i);
        
        /* Simulate some work */
        usleep(1000);  /* 1ms */
        
        /* Intentionally leak every 5th allocation if leak_some is true */
        if (args->leak_some && (i % 5 == 4)) {
            printf("[Thread %d] [!] Intentionally leaking allocation %d (%zu bytes)\n",
                   args->thread_id, i, size);
            atomic_fetch_add(&g_stats.intentional_leaks, size);
            continue;  /* Skip free - intentional leak! */
        }
        
        /* Normal cleanup */
        free(buffer);
        atomic_fetch_add(&g_stats.total_freed, size);
        atomic_fetch_add(&g_stats.free_count, 1);
    }
    
    printf("[Thread %d] Completed\n", args->thread_id);
    return NULL;
}

static void demonstrate_thread_local_pattern(bool with_leaks) {
    printf("\n═══════════════════════════════════════════════════════════════════\n");
    printf("Example 1: Thread-Local Allocation Pattern %s\n",
           with_leaks ? "(with intentional leaks)" : "(no leaks)");
    printf("═══════════════════════════════════════════════════════════════════\n");
    
    pthread_t threads[NUM_WORKER_THREADS];
    worker_args_t args[NUM_WORKER_THREADS];
    
    /* Start worker threads */
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        args[i].thread_id = i;
        args[i].allocations = ALLOCATIONS_PER_THREAD;
        args[i].leak_some = with_leaks;
        
        if (pthread_create(&threads[i], NULL, thread_local_worker, &args[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
        }
    }
    
    /* Wait for all threads to complete */
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\nThread-local pattern complete\n");
}

/* ============================================================================
 * Example 2: Producer-Consumer Pattern
 * ============================================================================
 * Producer threads allocate memory and put it in a queue.
 * Consumer threads take memory from the queue and free it.
 * This demonstrates memory ownership transfer between threads.
 */

static work_queue_t g_work_queue;
static atomic_int g_items_produced = 0;
static atomic_int g_items_consumed = 0;

static void* producer_thread(void* arg) {
    int producer_id = *(int*)arg;
    
    printf("[Producer %d] Starting\n", producer_id);
    
    for (int i = 0; i < 5; i++) {
        /* Allocate work item */
        size_t size = WORK_ITEM_SIZE + (size_t)(producer_id * 32);
        char* item = malloc(size);
        if (!item) {
            fprintf(stderr, "[Producer %d] malloc failed\n", producer_id);
            continue;
        }
        
        atomic_fetch_add(&g_stats.total_allocated, size);
        atomic_fetch_add(&g_stats.allocation_count, 1);
        
        snprintf(item, size, "Work item from producer %d, seq %d", 
                 producer_id, i);
        
        printf("[Producer %d] Created item %d (%zu bytes)\n", 
               producer_id, i, size);
        
        /* Push to queue - ownership transfers to consumer */
        if (!queue_push(&g_work_queue, item, size)) {
            /* Queue shut down, free the item ourselves */
            free(item);
            atomic_fetch_add(&g_stats.total_freed, size);
            atomic_fetch_add(&g_stats.free_count, 1);
            break;
        }
        
        atomic_fetch_add(&g_items_produced, 1);
        usleep(500);  /* Simulate production time */
    }
    
    printf("[Producer %d] Finished\n", producer_id);
    return NULL;
}

static void* consumer_thread(void* arg) {
    int consumer_id = *(int*)arg;
    
    printf("[Consumer %d] Starting\n", consumer_id);
    
    while (true) {
        void* item = NULL;
        size_t size = 0;
        
        if (!queue_pop(&g_work_queue, &item, &size)) {
            /* Queue empty and shutdown */
            break;
        }
        
        printf("[Consumer %d] Processing: %s\n", consumer_id, (char*)item);
        
        /* Simulate processing */
        usleep(1000);
        
        /* Free the item - we own it now */
        free(item);
        atomic_fetch_add(&g_stats.total_freed, size);
        atomic_fetch_add(&g_stats.free_count, 1);
        atomic_fetch_add(&g_items_consumed, 1);
    }
    
    printf("[Consumer %d] Finished\n", consumer_id);
    return NULL;
}

static void demonstrate_producer_consumer(void) {
    printf("\n═══════════════════════════════════════════════════════════════════\n");
    printf("Example 2: Producer-Consumer Pattern\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    
    queue_init(&g_work_queue);
    
    pthread_t producers[2];
    pthread_t consumers[2];
    int producer_ids[2] = {0, 1};
    int consumer_ids[2] = {0, 1};
    
    /* Start consumers first */
    for (int i = 0; i < 2; i++) {
        pthread_create(&consumers[i], NULL, consumer_thread, &consumer_ids[i]);
    }
    
    /* Start producers */
    for (int i = 0; i < 2; i++) {
        pthread_create(&producers[i], NULL, producer_thread, &producer_ids[i]);
    }
    
    /* Wait for producers to finish */
    for (int i = 0; i < 2; i++) {
        pthread_join(producers[i], NULL);
    }
    
    /* Signal consumers to shut down */
    printf("[Main] All producers done, signaling shutdown\n");
    usleep(100000);  /* Let consumers drain the queue */
    queue_shutdown(&g_work_queue);
    
    /* Wait for consumers */
    for (int i = 0; i < 2; i++) {
        pthread_join(consumers[i], NULL);
    }
    
    queue_destroy(&g_work_queue);
    
    printf("\nProducer-consumer pattern complete\n");
    printf("  Items produced: %d\n", atomic_load(&g_items_produced));
    printf("  Items consumed: %d\n", atomic_load(&g_items_consumed));
}

/* ============================================================================
 * Example 3: Shared Buffer with Reference Counting
 * ============================================================================
 * Demonstrates safe sharing of memory between threads using reference counting.
 */

typedef struct {
    char* data;
    size_t size;
    atomic_int ref_count;
    pthread_mutex_t mutex;
} shared_buffer_t;

static shared_buffer_t* shared_buffer_create(size_t size) {
    shared_buffer_t* buf = malloc(sizeof(shared_buffer_t));
    if (!buf) return NULL;
    
    buf->data = malloc(size);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    buf->size = size;
    atomic_init(&buf->ref_count, 1);
    pthread_mutex_init(&buf->mutex, NULL);
    
    atomic_fetch_add(&g_stats.total_allocated, sizeof(shared_buffer_t) + size);
    atomic_fetch_add(&g_stats.allocation_count, 2);
    
    return buf;
}

static void shared_buffer_retain(shared_buffer_t* buf) {
    atomic_fetch_add(&buf->ref_count, 1);
}

static void shared_buffer_release(shared_buffer_t* buf) {
    if (atomic_fetch_sub(&buf->ref_count, 1) == 1) {
        /* Last reference, free the buffer */
        size_t total_size = sizeof(shared_buffer_t) + buf->size;
        pthread_mutex_destroy(&buf->mutex);
        free(buf->data);
        free(buf);
        
        atomic_fetch_add(&g_stats.total_freed, total_size);
        atomic_fetch_add(&g_stats.free_count, 2);
    }
}

static shared_buffer_t* g_shared_buffer = NULL;

static void* shared_buffer_worker(void* arg) {
    int worker_id = *(int*)arg;
    
    /* Retain the shared buffer */
    shared_buffer_retain(g_shared_buffer);
    
    printf("[Worker %d] Acquired reference to shared buffer\n", worker_id);
    
    /* Safely access the buffer */
    pthread_mutex_lock(&g_shared_buffer->mutex);
    size_t len = strlen(g_shared_buffer->data);
    char append[32];
    snprintf(append, sizeof(append), " [W%d]", worker_id);
    if (len + strlen(append) < g_shared_buffer->size) {
        strcat(g_shared_buffer->data, append);
    }
    pthread_mutex_unlock(&g_shared_buffer->mutex);
    
    usleep(10000);  /* Simulate work */
    
    printf("[Worker %d] Releasing reference\n", worker_id);
    shared_buffer_release(g_shared_buffer);
    
    return NULL;
}

static void demonstrate_shared_buffer(void) {
    printf("\n═══════════════════════════════════════════════════════════════════\n");
    printf("Example 3: Shared Buffer with Reference Counting\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    
    /* Create shared buffer */
    g_shared_buffer = shared_buffer_create(256);
    if (!g_shared_buffer) {
        fprintf(stderr, "Failed to create shared buffer\n");
        return;
    }
    
    strcpy(g_shared_buffer->data, "Shared data");
    printf("[Main] Created shared buffer: \"%s\"\n", g_shared_buffer->data);
    
    /* Start workers */
    pthread_t workers[4];
    int worker_ids[4] = {0, 1, 2, 3};
    
    for (int i = 0; i < 4; i++) {
        pthread_create(&workers[i], NULL, shared_buffer_worker, &worker_ids[i]);
    }
    
    /* Wait for workers */
    for (int i = 0; i < 4; i++) {
        pthread_join(workers[i], NULL);
    }
    
    /* Print final buffer content */
    printf("[Main] Final buffer content: \"%s\"\n", g_shared_buffer->data);
    
    /* Release our reference */
    printf("[Main] Releasing main reference\n");
    shared_buffer_release(g_shared_buffer);
    g_shared_buffer = NULL;
    
    printf("\nShared buffer pattern complete\n");
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(void) {
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║      MemRogue Multithreaded Memory Allocation Examples          ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\nThis program demonstrates memory allocation patterns in\n");
    printf("multithreaded environments with some intentional leaks.\n");
    printf("Worker threads: %d, Allocations per thread: %d\n\n",
           NUM_WORKER_THREADS, ALLOCATIONS_PER_THREAD);
    
    /* Run examples */
    demonstrate_thread_local_pattern(false);  /* No leaks */
    demonstrate_thread_local_pattern(true);   /* With intentional leaks */
    demonstrate_producer_consumer();
    demonstrate_shared_buffer();
    
    /* Print final statistics */
    printf("\n════════════════════════════════════════════════════════════════════\n");
    printf("Final Statistics:\n");
    printf("  Total allocated:     %zu bytes\n", 
           atomic_load(&g_stats.total_allocated));
    printf("  Total freed:         %zu bytes\n", 
           atomic_load(&g_stats.total_freed));
    printf("  Allocation count:    %zu\n", 
           atomic_load(&g_stats.allocation_count));
    printf("  Free count:          %zu\n", 
           atomic_load(&g_stats.free_count));
    printf("  Intentional leaks:   %zu bytes\n", 
           atomic_load(&g_stats.intentional_leaks));
    printf("════════════════════════════════════════════════════════════════════\n");
    printf("\nRun with MemRogue to detect the intentional leaks:\n");
    printf("  LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/multithreaded_example\n\n");
    
    return EXIT_SUCCESS;
}
