/**
 * @file memrogue_config.h
 * @brief Runtime configuration via environment variables for MemRogue.
 *
 * This module provides a centralized configuration system that reads
 * environment variables to control the memory debugger's behavior.
 * Configuration is loaded once at initialization and cached for performance.
 *
 * Environment Variables:
 *   MEMROGUE_ENABLED      - Enable/disable tracking ("0", "1", "true", "false")
 *   MEMROGUE_OUTPUT       - Output file path (default: stderr)
 *   MEMROGUE_SAMPLE_RATE  - Sampling rate 1-100 (100 = track all, 1 = 1%)
 *   MEMROGUE_BACKTRACE    - Enable backtraces ("0", "1", "true", "false")
 *   MEMROGUE_VERBOSITY    - Verbosity level (0=quiet, 1=normal, 2=verbose, 3=debug)
 *   MEMROGUE_MAX_DEPTH    - Maximum backtrace depth (1-64)
 *   MEMROGUE_REPORT_ON_EXIT - Generate report on program exit ("0", "1")
 *   MEMROGUE_DETECT_DOUBLE_FREE - Detect double-free errors ("0", "1")
 *   MEMROGUE_DETECT_INVALID_FREE - Detect invalid free errors ("0", "1")
 *
 * Thread Safety:
 *   - config_load() should be called once at initialization
 *   - config_get() returns a pointer to read-only global config
 *   - Config reads are thread-safe after initialization only if config_reload() is not called concurrently.
 *   - If config_reload() is called concurrently with config reads, race conditions may occur and readers may access partially updated configuration.
 *   - To avoid race conditions during reload, callers must either avoid concurrent reloads or use config_load_into() to obtain a local, consistent copy.
 *
 * MEMRO-20: Environment Variable Configuration
 */

#ifndef MEMROGUE_CONFIG_H
#define MEMROGUE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ============================================================================
 * Environment Variable Names
 * ============================================================================ */

#define MEMROGUE_ENV_ENABLED            "MEMROGUE_ENABLED"
#define MEMROGUE_ENV_OUTPUT             "MEMROGUE_OUTPUT"
#define MEMROGUE_ENV_SAMPLE_RATE        "MEMROGUE_SAMPLE_RATE"
#define MEMROGUE_ENV_BACKTRACE          "MEMROGUE_BACKTRACE"
#define MEMROGUE_ENV_VERBOSITY          "MEMROGUE_VERBOSITY"
#define MEMROGUE_ENV_MAX_DEPTH          "MEMROGUE_MAX_DEPTH"
#define MEMROGUE_ENV_REPORT_ON_EXIT     "MEMROGUE_REPORT_ON_EXIT"
#define MEMROGUE_ENV_DETECT_DOUBLE_FREE "MEMROGUE_DETECT_DOUBLE_FREE"
#define MEMROGUE_ENV_DETECT_INVALID_FREE "MEMROGUE_DETECT_INVALID_FREE"

/* ============================================================================
 * Configuration Limits
 * ============================================================================ */

#define MEMROGUE_CONFIG_MIN_SAMPLE_RATE     1
#define MEMROGUE_CONFIG_MAX_SAMPLE_RATE     100
#define MEMROGUE_CONFIG_DEFAULT_SAMPLE_RATE 100

#define MEMROGUE_CONFIG_MIN_MAX_DEPTH       1
#define MEMROGUE_CONFIG_MAX_MAX_DEPTH       64
#define MEMROGUE_CONFIG_DEFAULT_MAX_DEPTH   16

#define MEMROGUE_CONFIG_MAX_OUTPUT_PATH     4096

/* ============================================================================
 * Verbosity Levels
 * ============================================================================ */

/**
 * Verbosity level for MemRogue output.
 */
typedef enum {
    MEMROGUE_VERBOSITY_QUIET   = 0,  /**< Only critical errors */
    MEMROGUE_VERBOSITY_NORMAL  = 1,  /**< Normal operation messages */
    MEMROGUE_VERBOSITY_VERBOSE = 2,  /**< Detailed operation info */
    MEMROGUE_VERBOSITY_DEBUG   = 3   /**< Debug-level output */
} memrogue_verbosity_t;

/* ============================================================================
 * Configuration Structure
 * ============================================================================ */

/**
 * MemRogue runtime configuration.
 *
 * This structure holds all configuration options parsed from environment
 * variables. It is designed to be read-only after initialization.
 */
typedef struct {
    /* Core tracking options */
    bool enabled;                   /**< Enable memory tracking */
    bool backtrace_enabled;         /**< Capture backtraces for allocations */
    int sample_rate;                /**< Sampling percentage (1-100) */
    int max_backtrace_depth;        /**< Maximum backtrace frames to capture */
    
    /* Output options */
    char output_path[MEMROGUE_CONFIG_MAX_OUTPUT_PATH]; /**< Output file path */
    bool output_to_file;            /**< True if output goes to file */
    memrogue_verbosity_t verbosity; /**< Verbosity level */
    
    /* Feature toggles */
    bool report_on_exit;            /**< Generate leak report at exit */
    bool detect_double_free;        /**< Detect double-free errors */
    bool detect_invalid_free;       /**< Detect invalid free errors */
    
    /* Internal state */
    bool _initialized;              /**< True if config has been loaded */
    uint32_t _load_count;           /**< Number of times config was loaded */
} memrogue_config_t;

/* ============================================================================
 * Configuration Lifecycle
 * ============================================================================ */

/**
 * Initialize configuration with default values.
 *
 * Sets all fields to their default values without reading environment
 * variables. Useful for testing or when environment parsing is not desired.
 *
 * @param config Configuration structure to initialize
 */
void config_init_defaults(memrogue_config_t* config);

/**
 * Load configuration from environment variables.
 *
 * Parses all MEMROGUE_* environment variables and populates the global
 * configuration. This function is thread-safe and idempotent - calling
 * it multiple times will reload the configuration.
 *
 * Should be called once at program/library initialization.
 *
 * @return Pointer to the global configuration (never NULL)
 */
const memrogue_config_t* config_load(void);

/**
 * Load configuration from environment into a specific structure.
 *
 * Unlike config_load(), this function loads into a user-provided structure
 * rather than the global config. Useful for testing.
 *
 * @param config Configuration structure to populate
 * @return true on success, false on error
 */
bool config_load_into(memrogue_config_t* config);

/**
 * Get the current global configuration.
 *
 * Returns a pointer to the read-only global configuration. If config_load()
 * has not been called, this will call it automatically.
 *
 * Thread-safe: Returns pointer to atomically-updated global config.
 *
 * @return Pointer to global configuration (never NULL)
 */
const memrogue_config_t* config_get(void);

/**
 * Reload configuration from environment variables.
 *
 * Forces a reload of all environment variables. Uses atomic operations
 * to ensure thread-safety during the update.
 *
 * @return Pointer to the updated global configuration
 */
const memrogue_config_t* config_reload(void);

/* ============================================================================
 * Configuration Queries
 * ============================================================================ */

/**
 * Check if memory tracking is enabled.
 *
 * Convenience function that checks the global config.
 *
 * @return true if tracking is enabled
 */
bool config_is_enabled(void);

/**
 * Check if backtraces are enabled.
 *
 * @return true if backtraces should be captured
 */
bool config_backtraces_enabled(void);

/**
 * Check if an allocation should be sampled.
 *
 * Uses the configured sample rate to determine if this allocation
 * should be tracked. Uses a fast PRNG for random sampling.
 *
 * Thread-safe: Uses thread-local PRNG state.
 *
 * @return true if allocation should be tracked
 */
bool config_should_sample(void);

/**
 * Get the current verbosity level.
 *
 * @return Current verbosity level
 */
memrogue_verbosity_t config_get_verbosity(void);

/**
 * Get the output stream for MemRogue messages.
 *
 * Returns the configured output stream (file or stderr).
 * Opens the file if not already open.
 *
 * Thread-safe: Uses internal locking for file access.
 *
 * @return Output FILE* stream, or stderr on error
 */
FILE* config_get_output_stream(void);

/**
 * Close the output stream if it's a file.
 *
 * Should be called at program exit to flush and close any output file.
 * Safe to call multiple times or if output is stderr.
 */
void config_close_output_stream(void);

/* ============================================================================
 * Environment Parsing Utilities
 * ============================================================================ */

/**
 * Parse a boolean value from an environment variable.
 *
 * Recognizes: "1", "true", "yes", "on" as true
 *             "0", "false", "no", "off" as false
 * Case-insensitive.
 *
 * @param env_name Environment variable name
 * @param default_value Value to return if variable is not set or invalid
 * @return Parsed boolean value
 */
bool config_parse_bool_env(const char* env_name, bool default_value);

/**
 * Parse an integer value from an environment variable.
 *
 * @param env_name Environment variable name
 * @param default_value Value to return if variable is not set or invalid
 * @param min_value Minimum allowed value (inclusive)
 * @param max_value Maximum allowed value (inclusive)
 * @return Parsed integer value, clamped to [min_value, max_value]
 */
int config_parse_int_env(const char* env_name, int default_value,
                         int min_value, int max_value);

/**
 * Parse a string value from an environment variable.
 *
 * @param env_name Environment variable name
 * @param buffer Output buffer for the string
 * @param buffer_size Size of the output buffer
 * @param default_value Value to use if variable is not set (may be NULL)
 * @return true if variable was found, false if using default
 */
bool config_parse_string_env(const char* env_name, char* buffer,
                             size_t buffer_size, const char* default_value);

/* ============================================================================
 * Debugging and Diagnostics
 * ============================================================================ */

/**
 * Print current configuration to a stream.
 *
 * Useful for debugging configuration issues.
 *
 * @param config Configuration to print (NULL for global config)
 * @param stream Output stream
 */
void config_print(const memrogue_config_t* config, FILE* stream);

/**
 * Get configuration as a human-readable string.
 *
 * @param config Configuration to format (NULL for global config)
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator)
 */
int config_to_string(const memrogue_config_t* config, char* buffer, size_t buffer_size);

#endif /* MEMROGUE_CONFIG_H */
