/**
 * @file custom_allocator.c
 * @brief Custom Allocator Example - Tracking Pool and Arena Allocators
 *
 * This example demonstrates how MemRogue can track memory allocated
 * through custom allocators that wrap standard malloc/free. It shows:
 *
 * 1. Pool Allocator - Fixed-size block allocation
 * 2. Arena Allocator - Bump allocator with batch free
 * 3. Slab Allocator - Object caching allocator
 *
 * Each allocator uses malloc/free internally, so MemRogue can track
 * the underlying allocations and detect leaks.
 *
 * Usage with MemRogue:
 *   LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/custom_allocator
 *
 * @author MemRogue Team
 * @date 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/* ==========================================================================
 * Pool Allocator - Fixed-size block allocation
 * ========================================================================== */

#define POOL_BLOCK_SIZE    64
#define POOL_BLOCK_COUNT   16

/**
 * @brief Pool block structure (intrusive linked list)
 */
typedef struct pool_block {
    struct pool_block *next;
    uint8_t data[POOL_BLOCK_SIZE - sizeof(struct pool_block *)];
} pool_block_t;

/**
 * @brief Pool allocator structure
 */
typedef struct {
    void *memory;           /* Raw memory chunk */
    pool_block_t *free_list; /* List of free blocks */
    size_t block_size;
    size_t block_count;
    size_t allocated_count;
} pool_allocator_t;

/**
 * @brief Initialize pool allocator
 * @return 0 on success, -1 on failure
 */
static int pool_init(pool_allocator_t *pool, size_t block_size, size_t block_count) {
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->allocated_count = 0;

    /* Allocate raw memory chunk (tracked by MemRogue) */
    size_t total_size = block_size * block_count;
    pool->memory = malloc(total_size);
    if (!pool->memory) {
        return -1;
    }

    /* Initialize free list */
    pool->free_list = NULL;
    uint8_t *ptr = (uint8_t *)pool->memory;
    for (size_t i = 0; i < block_count; i++) {
        pool_block_t *block = (pool_block_t *)ptr;
        block->next = pool->free_list;
        pool->free_list = block;
        ptr += block_size;
    }

    printf("[POOL] Initialized with %zu blocks of %zu bytes (total: %zu bytes)\n",
           block_count, block_size, total_size);

    return 0;
}

/**
 * @brief Allocate a block from the pool
 */
static void *pool_alloc(pool_allocator_t *pool) {
    if (!pool->free_list) {
        fprintf(stderr, "[POOL] Pool exhausted!\n");
        return NULL;
    }

    pool_block_t *block = pool->free_list;
    pool->free_list = block->next;
    pool->allocated_count++;

    return block;
}

/**
 * @brief Free a block back to the pool
 */
static void pool_free(pool_allocator_t *pool, void *ptr) {
    if (!ptr) return;

    pool_block_t *block = (pool_block_t *)ptr;
    block->next = pool->free_list;
    pool->free_list = block;
    pool->allocated_count--;
}

/**
 * @brief Destroy pool allocator
 */
static void pool_destroy(pool_allocator_t *pool) {
    printf("[POOL] Destroying pool (%zu blocks still allocated)\n",
           pool->allocated_count);

    /* If allocated_count > 0, there's a logical leak within the pool */
    /* The underlying memory is still freed by MemRogue */
    free(pool->memory);
    pool->memory = NULL;
    pool->free_list = NULL;
}

/* ==========================================================================
 * Arena Allocator - Bump allocator with batch free
 * ========================================================================== */

#define ARENA_DEFAULT_SIZE (4 * 1024)  /* 4KB */

/**
 * @brief Arena chunk structure
 */
typedef struct arena_chunk {
    struct arena_chunk *next;
    size_t size;
    size_t used;
    uint8_t data[];  /* Flexible array member */
} arena_chunk_t;

/**
 * @brief Arena allocator structure
 */
typedef struct {
    arena_chunk_t *chunks;       /* List of memory chunks */
    arena_chunk_t *current;      /* Current chunk for allocation */
    size_t default_chunk_size;
    size_t total_allocated;
    size_t total_used;
} arena_allocator_t;

/**
 * @brief Create a new arena chunk
 */
static arena_chunk_t *arena_create_chunk(size_t size) {
    arena_chunk_t *chunk = malloc(sizeof(arena_chunk_t) + size);
    if (!chunk) {
        return NULL;
    }

    chunk->next = NULL;
    chunk->size = size;
    chunk->used = 0;

    return chunk;
}

/**
 * @brief Initialize arena allocator
 * @return 0 on success, -1 on failure
 */
static int arena_init(arena_allocator_t *arena, size_t default_chunk_size) {
    arena->default_chunk_size = default_chunk_size > 0 ? default_chunk_size : ARENA_DEFAULT_SIZE;
    arena->total_allocated = 0;
    arena->total_used = 0;

    arena->chunks = arena_create_chunk(arena->default_chunk_size);
    if (!arena->chunks) {
        return -1;
    }

    arena->current = arena->chunks;
    arena->total_allocated = sizeof(arena_chunk_t) + arena->default_chunk_size;

    printf("[ARENA] Initialized with default chunk size: %zu bytes\n",
           arena->default_chunk_size);

    return 0;
}

/**
 * @brief Allocate memory from arena (bump allocation)
 * @note Memory cannot be individually freed, only reset all at once
 */
static void *arena_alloc(arena_allocator_t *arena, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~((size_t)7);

    /* Check if current chunk has space */
    if (arena->current->used + size > arena->current->size) {
        /* Need new chunk */
        size_t chunk_size = size > arena->default_chunk_size ? size : arena->default_chunk_size;
        arena_chunk_t *new_chunk = arena_create_chunk(chunk_size);
        if (!new_chunk) {
            return NULL;
        }

        new_chunk->next = arena->chunks;
        arena->chunks = new_chunk;
        arena->current = new_chunk;
        arena->total_allocated += sizeof(arena_chunk_t) + chunk_size;

        printf("[ARENA] Allocated new chunk of %zu bytes\n", chunk_size);
    }

    void *ptr = arena->current->data + arena->current->used;
    arena->current->used += size;
    arena->total_used += size;

    return ptr;
}

/**
 * @brief Reset arena (free all allocations but keep chunks)
 */
static void arena_reset(arena_allocator_t *arena) {
    arena_chunk_t *chunk = arena->chunks;
    while (chunk) {
        chunk->used = 0;
        chunk = chunk->next;
    }
    arena->current = arena->chunks;
    arena->total_used = 0;

    printf("[ARENA] Reset (memory reusable)\n");
}

/**
 * @brief Destroy arena allocator
 */
static void arena_destroy(arena_allocator_t *arena) {
    printf("[ARENA] Destroying arena (total allocated: %zu, total used: %zu)\n",
           arena->total_allocated, arena->total_used);

    arena_chunk_t *chunk = arena->chunks;
    while (chunk) {
        arena_chunk_t *next = chunk->next;
        free(chunk);
        chunk = next;
    }

    arena->chunks = NULL;
    arena->current = NULL;
    arena->total_allocated = 0;
    arena->total_used = 0;
}

/* ==========================================================================
 * Slab Allocator - Object caching allocator
 * ========================================================================== */

#define SLAB_OBJECTS_PER_SLAB 8

/**
 * @brief Slab structure
 */
typedef struct slab {
    struct slab *next;
    size_t free_count;
    uint8_t *free_bitmap;   /* Bitmap of free objects */
    uint8_t *objects;       /* Object storage */
} slab_t;

/**
 * @brief Slab cache (allocator) structure
 */
typedef struct {
    const char *name;
    size_t object_size;
    size_t objects_per_slab;
    slab_t *slabs;
    slab_t *partial;         /* Slabs with some free objects */
    size_t total_objects;
    size_t free_objects;
} slab_cache_t;

/**
 * @brief Create a new slab
 */
static slab_t *slab_create(size_t object_size, size_t objects_per_slab) {
    slab_t *slab = malloc(sizeof(slab_t));
    if (!slab) {
        return NULL;
    }

    size_t bitmap_size = (objects_per_slab + 7) / 8;
    slab->free_bitmap = malloc(bitmap_size);
    if (!slab->free_bitmap) {
        free(slab);
        return NULL;
    }

    slab->objects = malloc(object_size * objects_per_slab);
    if (!slab->objects) {
        free(slab->free_bitmap);
        free(slab);
        return NULL;
    }

    /* Mark all objects as free (1 = free, 0 = allocated) */
    memset(slab->free_bitmap, 0xFF, bitmap_size);
    slab->free_count = objects_per_slab;
    slab->next = NULL;

    return slab;
}

/**
 * @brief Destroy a slab
 */
static void slab_free(slab_t *slab) {
    if (!slab) return;
    free(slab->objects);
    free(slab->free_bitmap);
    free(slab);
}

/**
 * @brief Initialize slab cache
 * @return 0 on success, -1 on failure
 */
static int slab_cache_init(slab_cache_t *cache, const char *name,
                           size_t object_size, size_t objects_per_slab) {
    cache->name = name;
    cache->object_size = object_size;
    cache->objects_per_slab = objects_per_slab > 0 ? objects_per_slab : SLAB_OBJECTS_PER_SLAB;
    cache->total_objects = 0;
    cache->free_objects = 0;

    /* Create initial slab */
    cache->slabs = slab_create(object_size, cache->objects_per_slab);
    if (!cache->slabs) {
        return -1;
    }

    cache->partial = cache->slabs;
    cache->total_objects = cache->objects_per_slab;
    cache->free_objects = cache->objects_per_slab;

    printf("[SLAB:%s] Initialized with object size %zu, %zu objects per slab\n",
           name, object_size, cache->objects_per_slab);

    return 0;
}

/**
 * @brief Allocate object from slab cache
 */
static void *slab_alloc(slab_cache_t *cache) {
    /* Find a partial slab with free objects */
    slab_t *slab = cache->partial;
    if (!slab || slab->free_count == 0) {
        /* Need new slab */
        slab = slab_create(cache->object_size, cache->objects_per_slab);
        if (!slab) {
            return NULL;
        }

        slab->next = cache->slabs;
        cache->slabs = slab;
        cache->partial = slab;
        cache->total_objects += cache->objects_per_slab;
        cache->free_objects += cache->objects_per_slab;

        printf("[SLAB:%s] Created new slab\n", cache->name);
    }

    /* Find first free object in bitmap */
    for (size_t i = 0; i < cache->objects_per_slab; i++) {
        size_t byte_idx = i / 8;
        size_t bit_idx = i % 8;

        if (slab->free_bitmap[byte_idx] & (1 << bit_idx)) {
            /* Mark as allocated */
            slab->free_bitmap[byte_idx] &= (uint8_t)~(1 << bit_idx);
            slab->free_count--;
            cache->free_objects--;

            /* Update partial pointer if slab is now full */
            if (slab->free_count == 0) {
                /* Find next partial slab */
                cache->partial = NULL;
                for (slab_t *s = cache->slabs; s; s = s->next) {
                    if (s->free_count > 0) {
                        cache->partial = s;
                        break;
                    }
                }
            }

            return slab->objects + (i * cache->object_size);
        }
    }

    return NULL;  /* Should not reach here */
}

/**
 * @brief Free object back to slab cache
 */
static void slab_cache_free(slab_cache_t *cache, void *ptr) {
    if (!ptr) return;

    /* Find which slab this object belongs to */
    for (slab_t *slab = cache->slabs; slab; slab = slab->next) {
        uint8_t *start = slab->objects;
        uint8_t *end = start + (cache->object_size * cache->objects_per_slab);

        if ((uint8_t *)ptr >= start && (uint8_t *)ptr < end) {
            /* Found the slab */
            size_t offset = (size_t)((uint8_t *)ptr - start);
            size_t idx = offset / cache->object_size;

            size_t byte_idx = idx / 8;
            size_t bit_idx = idx % 8;

            /* Mark as free */
            slab->free_bitmap[byte_idx] |= (1 << bit_idx);
            slab->free_count++;
            cache->free_objects++;

            /* Update partial pointer */
            if (!cache->partial || cache->partial->free_count == 0) {
                cache->partial = slab;
            }

            return;
        }
    }

    fprintf(stderr, "[SLAB:%s] Invalid free - object not from this cache!\n",
            cache->name);
}

/**
 * @brief Destroy slab cache
 */
static void slab_cache_destroy(slab_cache_t *cache) {
    printf("[SLAB:%s] Destroying cache (total: %zu, free: %zu, leaked: %zu)\n",
           cache->name, cache->total_objects, cache->free_objects,
           cache->total_objects - cache->free_objects);

    slab_t *slab = cache->slabs;
    while (slab) {
        slab_t *next = slab->next;
        slab_free(slab);
        slab = next;
    }

    cache->slabs = NULL;
    cache->partial = NULL;
}

/* ==========================================================================
 * Example Usage and Demo
 * ========================================================================== */

/**
 * @brief Example object for slab allocator
 */
typedef struct {
    int id;
    char name[32];
    double value;
} example_object_t;

/**
 * @brief Demonstrate pool allocator
 */
static void demo_pool_allocator(void) {
    printf("\n=== Pool Allocator Demo ===\n");

    pool_allocator_t pool;
    if (pool_init(&pool, POOL_BLOCK_SIZE, POOL_BLOCK_COUNT) != 0) {
        fprintf(stderr, "Failed to initialize pool\n");
        return;
    }

    /* Allocate some blocks */
    void *blocks[10];
    for (int i = 0; i < 10; i++) {
        blocks[i] = pool_alloc(&pool);
        if (blocks[i]) {
            printf("[POOL] Allocated block %d at %p\n", i, blocks[i]);
            memset(blocks[i], i, POOL_BLOCK_SIZE);
        }
    }

    /* Free some blocks */
    for (int i = 0; i < 7; i++) {
        pool_free(&pool, blocks[i]);
        printf("[POOL] Freed block %d\n", i);
    }

    /* INTENTIONAL LEAK: Don't free blocks 7, 8, 9 */
    printf("[POOL] Intentionally leaking 3 blocks (logical leak within pool)\n");

    /* Destroy pool - underlying memory freed but logical leaks detected */
    pool_destroy(&pool);
}

/**
 * @brief Demonstrate arena allocator
 */
static void demo_arena_allocator(void) {
    printf("\n=== Arena Allocator Demo ===\n");

    arena_allocator_t arena;
    if (arena_init(&arena, 1024) != 0) {
        fprintf(stderr, "Failed to initialize arena\n");
        return;
    }

    /* Phase 1: Allocate some strings */
    printf("[ARENA] Phase 1: Allocating strings\n");
    for (int i = 0; i < 5; i++) {
        char *str = arena_alloc(&arena, 64);
        if (str) {
            snprintf(str, 64, "String number %d", i);
            printf("[ARENA] Allocated: '%s' at %p\n", str, (void *)str);
        }
    }

    /* Phase 2: Allocate larger objects */
    printf("[ARENA] Phase 2: Allocating large objects\n");
    for (int i = 0; i < 3; i++) {
        void *obj = arena_alloc(&arena, 512);
        if (obj) {
            printf("[ARENA] Allocated 512 bytes at %p\n", obj);
        }
    }

    /* Reset and reuse */
    arena_reset(&arena);

    /* Phase 3: New allocations after reset */
    printf("[ARENA] Phase 3: Allocating after reset\n");
    for (int i = 0; i < 3; i++) {
        int *arr = arena_alloc(&arena, sizeof(int) * 10);
        if (arr) {
            for (int j = 0; j < 10; j++) {
                arr[j] = i * 10 + j;
            }
            printf("[ARENA] Allocated int array at %p\n", (void *)arr);
        }
    }

    /* INTENTIONAL LEAK SCENARIO: Create new arena and "forget" to destroy it */
    printf("[ARENA] Creating secondary arena (will be leaked)\n");
    arena_allocator_t *leaked_arena = malloc(sizeof(arena_allocator_t));
    if (leaked_arena) {
        if (arena_init(leaked_arena, 512) == 0) {
            arena_alloc(leaked_arena, 256);
            arena_alloc(leaked_arena, 128);
            /* NOT calling arena_destroy - intentional leak */
            printf("[ARENA] Secondary arena NOT destroyed (leak!)\n");
            /* Note: In real code, we should destroy this */
            /* We're also leaking the arena struct itself */
        }
    }

    /* Destroy primary arena */
    arena_destroy(&arena);
}

/**
 * @brief Demonstrate slab allocator
 */
static void demo_slab_allocator(void) {
    printf("\n=== Slab Allocator Demo ===\n");

    slab_cache_t cache;
    if (slab_cache_init(&cache, "example_obj", sizeof(example_object_t), 4) != 0) {
        fprintf(stderr, "Failed to initialize slab cache\n");
        return;
    }

    /* Allocate objects */
    example_object_t *objects[10];
    for (int i = 0; i < 10; i++) {
        objects[i] = slab_alloc(&cache);
        if (objects[i]) {
            objects[i]->id = i;
            snprintf(objects[i]->name, sizeof(objects[i]->name), "Object_%d", i);
            objects[i]->value = i * 1.5;
            printf("[SLAB] Allocated object %d: id=%d, name='%s', value=%.1f\n",
                   i, objects[i]->id, objects[i]->name, objects[i]->value);
        }
    }

    /* Free some objects */
    printf("[SLAB] Freeing objects 0, 2, 4, 6, 8\n");
    for (int i = 0; i < 10; i += 2) {
        slab_cache_free(&cache, objects[i]);
        objects[i] = NULL;
    }

    /* Reallocate (should reuse freed slots) */
    printf("[SLAB] Reallocating 3 objects\n");
    for (int i = 0; i < 3; i++) {
        example_object_t *obj = slab_alloc(&cache);
        if (obj) {
            obj->id = 100 + i;
            snprintf(obj->name, sizeof(obj->name), "Reused_%d", i);
            obj->value = 100.0 + i;
            printf("[SLAB] Reallocated: id=%d, name='%s'\n", obj->id, obj->name);
        }
    }

    /* INTENTIONAL LEAK: Don't free remaining objects */
    printf("[SLAB] Intentionally not freeing remaining objects\n");

    /* Destroy cache - reports logical leaks */
    slab_cache_destroy(&cache);
}

/* ==========================================================================
 * Main Function
 * ========================================================================== */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("MemRogue Custom Allocator Example\n");
    printf("==================================\n");
    printf("This example demonstrates how MemRogue tracks memory used by\n");
    printf("custom allocators (pool, arena, slab).\n\n");
    printf("Run with MemRogue to see:\n");
    printf("  - Underlying malloc/free calls from custom allocators\n");
    printf("  - Memory leaks when allocators are not properly destroyed\n");
    printf("  - Logical leaks within allocators (allocated but not freed)\n\n");

    /* Demonstrate each allocator type */
    demo_pool_allocator();
    demo_arena_allocator();
    demo_slab_allocator();

    printf("\n=== Summary ===\n");
    printf("The custom allocators use malloc/free internally.\n");
    printf("MemRogue tracks these underlying allocations.\n");
    printf("Leaks detected include:\n");
    printf("  1. Pool: 3 blocks not returned to pool before destroy\n");
    printf("  2. Arena: Secondary arena never destroyed\n");
    printf("  3. Slab: Objects allocated but not freed before destroy\n");
    printf("\nCheck MemRogue output for detailed leak information.\n");

    return 0;
}
