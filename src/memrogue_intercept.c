#define _GNU_SOURCE

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "memrogue_hash_table.h"

typedef void *(*malloc_fn)(size_t size);
typedef void *(*calloc_fn)(size_t nmemb, size_t size);
typedef void *(*realloc_fn)(void *ptr, size_t size);
typedef void (*free_fn)(void *ptr);

typedef struct memrogue_hooks {
    malloc_fn real_malloc;
    calloc_fn real_calloc;
    realloc_fn real_realloc;
    free_fn real_free;
} memrogue_hooks_t;

static memrogue_hooks_t g_hooks = {0};
static hash_table_t *g_alloc_table = NULL;
static __thread bool g_reentry_guard = false;
static bool g_shutdown = false;
static bool g_initialized = false;

#if defined(__GLIBC__)
extern void *__libc_malloc(size_t size);
extern void *__libc_calloc(size_t nmemb, size_t size);
extern void *__libc_realloc(void *ptr, size_t size);
extern void __libc_free(void *ptr);
#endif

static inline void *memrogue_raw_malloc(size_t size) {
    if (g_hooks.real_malloc) {
        return g_hooks.real_malloc(size);
    }
#if defined(__GLIBC__)
    return __libc_malloc(size);
#else
    return NULL;
#endif
}

static inline void *memrogue_raw_calloc(size_t nmemb, size_t size) {
    if (g_hooks.real_calloc) {
        return g_hooks.real_calloc(nmemb, size);
    }
#if defined(__GLIBC__)
    return __libc_calloc(nmemb, size);
#else
    return NULL;
#endif
}

static inline void *memrogue_raw_realloc(void *ptr, size_t size) {
    if (g_hooks.real_realloc) {
        return g_hooks.real_realloc(ptr, size);
    }
#if defined(__GLIBC__)
    return __libc_realloc(ptr, size);
#else
    (void)ptr;
    return NULL;
#endif
}

static inline void memrogue_raw_free(void *ptr) {
    if (g_hooks.real_free) {
        g_hooks.real_free(ptr);
        return;
    }
#if defined(__GLIBC__)
    __libc_free(ptr);
#else
    (void)ptr;
#endif
}

static void memrogue_report(const char *message, void *ptr) {
    fprintf(stderr, "[MemRogue] %s (ptr=%p)\n", message, ptr);
}

static void memrogue_track_alloc(void *ptr, size_t size) {
    if (!ptr || !g_alloc_table) {
        return;
    }
    if (!hash_table_insert(g_alloc_table, ptr, size, "unknown", 0)) {
        memrogue_report("Warning: failed to record allocation", ptr);
    }
}

static void memrogue_track_free(void *ptr) {
    if (!ptr || !g_alloc_table) {
        return;
    }
    if (!hash_table_remove(g_alloc_table, ptr)) {
        memrogue_report("Warning: free on untracked pointer", ptr);
    }
}

static void memrogue_assign_symbol(void **target, const char *symbol) {
    void *sym = dlsym(RTLD_NEXT, symbol);
    if (!sym) {
        fprintf(stderr, "[MemRogue] Failed to resolve %s\n", symbol);
        return;
    }
    memcpy(target, &sym, sizeof(sym));
}

static void memrogue_initialize(void) {
    if (g_initialized) return;
    g_initialized = true;
    
    g_reentry_guard = true;
    dlerror();
    memrogue_assign_symbol((void **)&g_hooks.real_malloc, "malloc");
    memrogue_assign_symbol((void **)&g_hooks.real_calloc, "calloc");
    memrogue_assign_symbol((void **)&g_hooks.real_realloc, "realloc");
    memrogue_assign_symbol((void **)&g_hooks.real_free, "free");

    const char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "[MemRogue] dlsym error: %s\n", error);
    }

    g_alloc_table = hash_table_create(2048);
    if (!g_alloc_table) {
        fprintf(stderr, "[MemRogue] Failed to initialize allocation table\n");
    }
    g_reentry_guard = false;
}

__attribute__((constructor))
static void memrogue_init_constructor(void) {
    memrogue_initialize();
}

static void memrogue_ensure_initialized(void) {
    if (!g_shutdown && !g_initialized) {
        memrogue_initialize();
    }
}

static inline bool memrogue_should_bypass(void) {
    return g_shutdown || g_reentry_guard;
}

void *malloc(size_t size) {
    memrogue_ensure_initialized();
    if (!g_hooks.real_malloc || memrogue_should_bypass()) {
        return memrogue_raw_malloc(size);
    }

    g_reentry_guard = true;
    void *ptr = g_hooks.real_malloc(size);
    if (ptr) {
        memrogue_track_alloc(ptr, size);
    }
    g_reentry_guard = false;
    return ptr;
}

void free(void *ptr) {
    memrogue_ensure_initialized();
    if (!g_hooks.real_free || memrogue_should_bypass()) {
        memrogue_raw_free(ptr);
        return;
    }

    g_reentry_guard = true;
    memrogue_track_free(ptr);
    g_hooks.real_free(ptr);
    g_reentry_guard = false;
}

void *calloc(size_t nmemb, size_t size) {
    memrogue_ensure_initialized();
    if (!g_hooks.real_calloc || memrogue_should_bypass()) {
        return memrogue_raw_calloc(nmemb, size);
    }

    g_reentry_guard = true;
    void *ptr = g_hooks.real_calloc(nmemb, size);
    if (ptr) {
        size_t total = nmemb * size;
        if (nmemb != 0 && total / nmemb != size) {
            memrogue_report("Overflow detected in calloc parameters", ptr);
        } else {
            memrogue_track_alloc(ptr, total);
        }
    }
    g_reentry_guard = false;
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    memrogue_ensure_initialized();
    if (!g_hooks.real_realloc || memrogue_should_bypass()) {
        return memrogue_raw_realloc(ptr, size);
    }

    g_reentry_guard = true;

    if (size == 0) {
        memrogue_track_free(ptr);
        void *result = g_hooks.real_realloc(ptr, size);
        g_reentry_guard = false;
        return result;
    }

    void *result = g_hooks.real_realloc(ptr, size);
    if (result) {
        if (ptr) {
            memrogue_track_free(ptr);
        }
        memrogue_track_alloc(result, size);
    }

    g_reentry_guard = false;
    return result;
}

__attribute__((destructor))
static void memrogue_shutdown(void) {
    g_shutdown = true;
    g_reentry_guard = true;
    if (g_alloc_table) {
        size_t outstanding = hash_table_count(g_alloc_table);
        if (outstanding > 0) {
            fprintf(stderr,
                    "[MemRogue] Detected %zu outstanding allocation(s) at shutdown\n",
                    outstanding);
        } else {
            fprintf(stderr, "[MemRogue] No outstanding allocations detected\n");
        }
        hash_table_destroy(g_alloc_table);
        g_alloc_table = NULL;
    }
    g_reentry_guard = false;
}
