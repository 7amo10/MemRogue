/**
 * @file memrogue_exit_handler.c
 * @brief Exit handler implementation for automatic leak detection at program termination.
 *
 * This module provides exit hooks using both atexit() and __attribute__((destructor))
 * to ensure leak detection runs at program termination. Thread-safe registration
 * and unregistration are supported.
 *
 * MEMRO-13: Exit Hook Implementation
 */

#include "memrogue_exit_handler.h"
#include "memrogue_tracker.h"
#include "memrogue_allocation_record.h"
#include "memrogue_backtrace.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

/** Mutex for thread-safe access to handler state */
static pthread_mutex_t g_handler_mutex = PTHREAD_MUTEX_INITIALIZER;

/** Flag indicating if exit handler is registered */
static atomic_bool g_handler_registered = false;

/** Flag indicating if atexit handler has already run (prevent double execution) */
static atomic_bool g_handler_executed = false;

/** Flag indicating if destructor-based handling is enabled */
static atomic_bool g_destructor_enabled = true;

/** Current configuration */
static exit_handler_config_t g_config = {
    .enabled = true,
    .print_report_on_exit = true,
    .abort_on_leaks = false,
    .leak_threshold = 0,
    .report_file = NULL
};

/** Owned copy of report file path (strdup'd to avoid use-after-free) */
static char* g_report_file_owned = NULL;

/** Pointer to the memory tracker to use for leak detection */
static memory_tracker_t* g_tracker = NULL;

/** Custom callback function */
static exit_handler_callback_t g_callback = NULL;

/** User data for callback */
static void* g_callback_user_data = NULL;

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/** Context for leak printing callback */
typedef struct {
    FILE* out;
    size_t leak_index;
} leak_print_context_t;

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

/**
 * @brief Callback for iterating over leaked allocations to print report.
 * @param info The allocation info.
 * @param user_data Pointer to leak_print_context_t.
 * @return true to continue iteration.
 */
static bool print_leak_callback(const allocation_info_t* info, void* user_data) {
    leak_print_context_t* ctx = (leak_print_context_t*)user_data;
    ctx->leak_index++;
    
    fprintf(ctx->out, "[MemRogue] Leak #%zu: %zu bytes at %p\n",
            ctx->leak_index, info->size, info->ptr);
    
    /* Print source location if available */
    if (info->file != NULL) {
        fprintf(ctx->out, "[MemRogue]   Allocated at: %s:%d\n",
                info->file, info->line);
    }
    
    /* Print backtrace if available */
    if (info->frame_count > 0) {
        fprintf(ctx->out, "[MemRogue]   Backtrace:\n");
        for (int i = 0; i < info->frame_count; i++) {
            /* Resolve symbol for frame address */
            resolved_frame_t frame;
            if (symbol_resolve_frame(info->frames[i], &frame)) {
                fprintf(ctx->out, "[MemRogue]     #%d: %s", i, 
                        frame.function_name ? frame.function_name : frame.symbol);
                if (frame.file_name != NULL) {
                    fprintf(ctx->out, " (%s:%d)", frame.file_name, frame.line_number);
                }
                fprintf(ctx->out, "\n");
            } else {
                fprintf(ctx->out, "[MemRogue]     #%d: %p\n", i, info->frames[i]);
            }
        }
    }
    
    return true;  /* Continue iteration */
}

/**
 * @brief Core exit handler logic - called by both atexit and destructor.
 *
 * Uses atomic flag to ensure handler only executes once even if both
 * atexit() and destructor are triggered.
 *
 * @return Number of leaked allocations found.
 */
static size_t exit_handler_core(void) {
    /* Atomically check and set executed flag to prevent double execution */
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&g_handler_executed, &expected, true,
                                                  memory_order_acq_rel, memory_order_acquire)) {
        return 0;  /* Already executed */
    }
    
    /* Check if handler is enabled and registered */
    pthread_mutex_lock(&g_handler_mutex);
    
    if (!atomic_load_explicit(&g_handler_registered, memory_order_acquire) || !g_config.enabled) {
        pthread_mutex_unlock(&g_handler_mutex);
        return 0;
    }
    
    memory_tracker_t* tracker = g_tracker;
    exit_handler_config_t config = g_config;
    exit_handler_callback_t callback = g_callback;
    void* callback_data = g_callback_user_data;
    
    pthread_mutex_unlock(&g_handler_mutex);
    
    if (tracker == NULL) {
        return 0;  /* No tracker set, nothing to report */
    }
    
    /* Get statistics */
    tracker_stats_t stats;
    tracker_get_stats(tracker, &stats);
    
    size_t leaked_count = (size_t)stats.active_allocations;
    size_t leaked_bytes = (size_t)stats.active_bytes;
    
    /* Invoke custom callback if set */
    if (callback != NULL) {
        callback(tracker, leaked_count, leaked_bytes, callback_data);
    }
    
    /* Print leak report if configured and leaks exist */
    if (config.print_report_on_exit && leaked_count > 0) {
        FILE* out = stderr;
        FILE* file_out = NULL;
        
        /* Open report file if specified */
        if (config.report_file != NULL) {
            file_out = fopen(config.report_file, "w");
            if (file_out != NULL) {
                out = file_out;
            }
        }
        
        fprintf(out, "[MemRogue] === Memory Leak Report ===\n");
        fprintf(out, "[MemRogue] Detected %zu leaked allocation(s), %zu bytes total\n",
                leaked_count, leaked_bytes);
        fprintf(out, "[MemRogue]\n");
        
        /* Iterate and print each leak */
        leak_print_context_t ctx = { .out = out, .leak_index = 0 };
        tracker_iterate(tracker, print_leak_callback, &ctx);
        
        /* Print summary statistics */
        char* stats_str = tracker_format_stats(tracker);
        if (stats_str != NULL) {
            fprintf(out, "[MemRogue]\n");
            fprintf(out, "[MemRogue] %s", stats_str);
            free(stats_str);
        }
        fprintf(out, "[MemRogue] === End Leak Report ===\n");
        
        if (file_out != NULL) {
            fclose(file_out);
        }
    }
    
    /* Abort if configured and leaks exceed threshold */
    if (config.abort_on_leaks && leaked_bytes > config.leak_threshold) {
        fprintf(stderr, "[MemRogue] Aborting due to memory leaks "
                "(leaked_bytes=%zu > threshold=%zu)\n",
                leaked_bytes, config.leak_threshold);
        fflush(stderr);
        abort();
    }
    
    return leaked_count;
}

/**
 * @brief atexit() callback function.
 *
 * This is registered via atexit() and called during normal program termination.
 */
static void atexit_handler(void) {
    exit_handler_core();
}

/**
 * @brief Destructor function called at shared library unload or program exit.
 *
 * Uses GCC/Clang __attribute__((destructor)) to ensure cleanup even in
 * some abnormal termination scenarios.
 *
 * Priority 101 means this runs after default destructors (priority 65535)
 * but users should use lower priorities (< 101) for their own cleanup
 * destructors if they want them to run before leak detection.
 */
__attribute__((destructor))
static void destructor_handler(void) {
    if (atomic_load_explicit(&g_destructor_enabled, memory_order_acquire)) {
        exit_handler_core();
    }
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

void exit_handler_config_init(exit_handler_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    config->enabled = true;
    config->print_report_on_exit = true;
    config->abort_on_leaks = false;
    config->leak_threshold = 0;
    config->report_file = NULL;
}

bool exit_handler_register(memory_tracker_t* tracker, const exit_handler_config_t* config) {
    pthread_mutex_lock(&g_handler_mutex);
    
    /* Store tracker reference */
    g_tracker = tracker;
    
    /* Free previous owned report file path if any */
    if (g_report_file_owned != NULL) {
        free(g_report_file_owned);
        g_report_file_owned = NULL;
    }
    
    /* Apply configuration */
    if (config != NULL) {
        g_config = *config;
        /* Make owned copy of report_file to avoid use-after-free */
        if (config->report_file != NULL) {
            g_report_file_owned = strdup(config->report_file);
            if (g_report_file_owned == NULL) {
                /* strdup failed - log warning and disable file output */
                fprintf(stderr, "[MemRogue] WARNING: Failed to allocate memory for report file path\n");
                g_config.report_file = NULL;
            } else {
                g_config.report_file = g_report_file_owned;
            }
        }
    } else {
        exit_handler_config_init(&g_config);
    }
    
    /* Check if already registered - if so, just update config and tracker */
    if (atomic_load_explicit(&g_handler_registered, memory_order_acquire)) {
        pthread_mutex_unlock(&g_handler_mutex);
        return true;
    }
    
    /* Register atexit handler */
    if (atexit(atexit_handler) != 0) {
        pthread_mutex_unlock(&g_handler_mutex);
        return false;
    }
    
    /* Mark as registered */
    atomic_store_explicit(&g_handler_registered, true, memory_order_release);
    atomic_store_explicit(&g_handler_executed, false, memory_order_release);  /* Reset executed flag */
    
    pthread_mutex_unlock(&g_handler_mutex);
    return true;
}

void exit_handler_unregister(void) {
    pthread_mutex_lock(&g_handler_mutex);
    
    /* Note: atexit() handlers cannot be unregistered from the atexit list,
     * but we can mark as disabled so the handler becomes a no-op */
    g_config.enabled = false;
    g_tracker = NULL;
    
    /* Free owned report file path */
    if (g_report_file_owned != NULL) {
        free(g_report_file_owned);
        g_report_file_owned = NULL;
        g_config.report_file = NULL;
    }
    
    atomic_store_explicit(&g_handler_registered, false, memory_order_release);
    
    pthread_mutex_unlock(&g_handler_mutex);
}

bool exit_handler_is_registered(void) {
    pthread_mutex_lock(&g_handler_mutex);
    bool registered = atomic_load_explicit(&g_handler_registered, memory_order_acquire) && g_config.enabled;
    pthread_mutex_unlock(&g_handler_mutex);
    return registered;
}

size_t exit_handler_run_now(void) {
    pthread_mutex_lock(&g_handler_mutex);
    /* Reset the executed flag to allow manual trigger (under mutex to avoid race) */
    atomic_store_explicit(&g_handler_executed, false, memory_order_release);
    pthread_mutex_unlock(&g_handler_mutex);
    /* Call core after releasing mutex - core will re-acquire it */
    return exit_handler_core();
}

void exit_handler_set_callback(exit_handler_callback_t callback, void* user_data) {
    pthread_mutex_lock(&g_handler_mutex);
    g_callback = callback;
    g_callback_user_data = user_data;
    pthread_mutex_unlock(&g_handler_mutex);
}

void exit_handler_set_destructor_enabled(bool enable) {
    atomic_store_explicit(&g_destructor_enabled, enable, memory_order_release);
}

bool exit_handler_is_destructor_enabled(void) {
    return atomic_load_explicit(&g_destructor_enabled, memory_order_acquire);
}
