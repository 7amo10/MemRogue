/**
 * @file memrogue_exit_handler.h
 * @brief Exit handler for automatic leak detection at program termination
 * 
 * This module provides functionality to register cleanup handlers that run
 * at program exit to perform leak detection and generate reports.
 * 
 * Supports two registration mechanisms:
 * 1. atexit() - Standard C library exit handler
 * 2. __attribute__((destructor)) - GCC/Clang destructor attribute
 * 
 * MEMRO-13: Exit Hook Implementation
 */

#ifndef MEMROGUE_EXIT_HANDLER_H
#define MEMROGUE_EXIT_HANDLER_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declare the tracker struct - must match definition in memrogue_tracker.h */
typedef struct memory_tracker_internal memory_tracker_t;

// ============================================================================
// Exit Handler Configuration
// ============================================================================

/**
 * Configuration options for exit handler behavior.
 */
typedef struct {
    bool enabled;                    // Whether exit handler is active
    bool print_report_on_exit;       // Print leak report to stderr on exit
    bool abort_on_leaks;             // Call abort() if leaks are detected
    size_t leak_threshold;           // Minimum bytes to trigger abort (0 = any leak)
    const char* report_file;         // Optional file path to write report (NULL = none)
} exit_handler_config_t;

// ============================================================================
// Exit Handler Lifecycle
// ============================================================================

/**
 * Initialize exit handler configuration with default values.
 * 
 * Defaults:
 *   - enabled: true
 *   - print_report_on_exit: true
 *   - abort_on_leaks: false
 *   - leak_threshold: 0
 *   - report_file: NULL
 * 
 * @param config The configuration structure to initialize
 */
void exit_handler_config_init(exit_handler_config_t* config);

/**
 * Register the exit handler with the specified tracker and configuration.
 * 
 * This function registers a cleanup handler using atexit() that will:
 * 1. Check for memory leaks in the tracker
 * 2. Generate a leak report if leaks are found
 * 3. Optionally abort if leaks exceed threshold
 * 
 * Only one exit handler can be active at a time. Calling this function
 * again will replace the previous configuration.
 * 
 * @param tracker The memory tracker to check for leaks (must remain valid until exit)
 * @param config Configuration options (NULL for defaults)
 * @return true if registration succeeded, false otherwise
 */
bool exit_handler_register(memory_tracker_t* tracker, const exit_handler_config_t* config);

/**
 * Unregister the exit handler.
 * 
 * Disables the exit handler so it won't run at program termination.
 * Note: Due to atexit() limitations, the handler function remains registered
 * but will do nothing when called.
 */
void exit_handler_unregister(void);

/**
 * Check if an exit handler is currently registered and active.
 * 
 * @return true if an exit handler is registered and enabled
 */
bool exit_handler_is_registered(void);

// ============================================================================
// Manual Invocation
// ============================================================================

/**
 * Manually trigger the exit handler.
 * 
 * This can be called at any time to perform leak detection without
 * waiting for program termination. Useful for:
 * - Testing the exit handler
 * - Periodic leak checks during long-running programs
 * - Manual cleanup before exec() or similar
 * 
 * @return Number of leaked allocations found (0 = no leaks)
 */
size_t exit_handler_run_now(void);

/**
 * Callback function type for custom exit handlers.
 * 
 * @param tracker The memory tracker being checked
 * @param leaked_count Number of leaked allocations
 * @param leaked_bytes Total bytes leaked
 * @param user_data User-provided context
 */
typedef void (*exit_handler_callback_t)(
    memory_tracker_t* tracker,
    size_t leaked_count,
    size_t leaked_bytes,
    void* user_data
);

/**
 * Set a custom callback to be invoked during exit handling.
 * 
 * The callback is called after leak detection but before report generation.
 * This allows custom processing of leak data.
 * 
 * @param callback The callback function (NULL to remove)
 * @param user_data User context passed to callback
 */
void exit_handler_set_callback(exit_handler_callback_t callback, void* user_data);

// ============================================================================
// Destructor Attribute Support
// ============================================================================

/**
 * Enable automatic registration using __attribute__((destructor)).
 * 
 * When enabled, the exit handler will automatically run as a destructor
 * function, which executes after main() returns but before the process
 * terminates. This provides an additional safety net beyond atexit().
 * 
 * Note: This is enabled by default when the library is loaded via LD_PRELOAD.
 * 
 * @param enable true to enable destructor, false to disable
 */
void exit_handler_set_destructor_enabled(bool enable);

/**
 * Check if destructor-based handling is enabled.
 * 
 * @return true if destructor handling is enabled
 */
bool exit_handler_is_destructor_enabled(void);

#endif // MEMROGUE_EXIT_HANDLER_H
