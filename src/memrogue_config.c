/**
 * @file memrogue_config.c
 * @brief Implementation of environment variable configuration system.
 *
 * This module implements thread-safe configuration parsing from environment
 * variables. It uses atomic operations for the global config and provides
 * a fast PRNG for sampling decisions.
 *
 * Design Goals:
 * - Thread-safe configuration access
 * - Fast sampling decisions (no locks in hot path)
 * - Graceful handling of invalid environment values
 * - No memory leaks (all allocations are static or managed)
 *
 * MEMRO-20: Environment Variable Configuration
 * MEMRO-21: Sampling Mode (random/deterministic)
 */

#define _POSIX_C_SOURCE 200809L

#include "memrogue_config.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ============================================================================
 * Global State
 * ============================================================================ */

/**
 * Global configuration instance.
 * Access is protected by atomic operations and mutex for updates.
 */
static memrogue_config_t g_config;

/**
 * Mutex for protecting configuration updates.
 */
static pthread_mutex_t g_config_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Atomic flag indicating if config has been initialized.
 */
static atomic_bool g_config_initialized = false;

/**
 * Output file stream (if writing to file).
 */
static FILE* g_output_stream = NULL;
static pthread_mutex_t g_output_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Thread-local PRNG state for sampling.
 */
static _Thread_local uint32_t tl_prng_state = 0;
static _Thread_local bool tl_prng_initialized = false;

/**
 * Thread-local deterministic sampling counter.
 * MEMRO-21: Sampling Mode
 */
static _Thread_local uint64_t tl_sample_counter = 0;

/* ============================================================================
 * Internal Utilities
 * ============================================================================ */

/**
 * Fast xorshift32 PRNG for sampling decisions.
 * Returns a value in [0, UINT32_MAX].
 */
static uint32_t prng_next(void) {
    if (!tl_prng_initialized) {
        /* Seed with high-resolution time, thread ID, and stack address for uniqueness */
        struct timespec ts;
        uintptr_t tid = (uintptr_t)pthread_self();
        uintptr_t stack_addr = (uintptr_t)&ts; /* address of local variable */
        uint32_t seed;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            seed = (uint32_t)(ts.tv_sec) ^ (uint32_t)(ts.tv_nsec);
        } else {
            seed = (uint32_t)time(NULL);
        }
        /* Mix all entropy sources using MurmurHash3 finalizer */
        seed ^= (uint32_t)tid;
        seed ^= (uint32_t)stack_addr;
        seed ^= seed >> 16;
        seed *= 0x85ebca6b;
        seed ^= seed >> 13;
        seed *= 0xc2b2ae35;
        seed ^= seed >> 16;
        tl_prng_state = seed ? seed : 1; /* Avoid zero state */
        tl_prng_initialized = true;
    }
    
    uint32_t x = tl_prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    tl_prng_state = x;
    return x;
}

/**
 * Convert string to lowercase for case-insensitive comparison.
 * Modifies the string in place.
 */
static void str_to_lower(char* str) {
    if (str == NULL) return;
    for (char* p = str; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

/**
 * Parse sampling mode from environment variable.
 * MEMRO-21: Sampling Mode
 *
 * Recognizes: "random", "rand", "r" as random mode
 *             "deterministic", "det", "d", "nth" as deterministic mode
 * Case-insensitive. Default is random mode.
 *
 * @param env_name Environment variable name
 * @param default_mode Value to return if variable is not set or invalid
 * @return Parsed sampling mode
 */
static memrogue_sampling_mode_t parse_sampling_mode_env(
    const char* env_name, memrogue_sampling_mode_t default_mode) {
    
    if (env_name == NULL) {
        return default_mode;
    }
    
    const char* value = getenv(env_name);
    if (value == NULL || *value == '\0') {
        return default_mode;
    }
    
    /* Make a lowercase copy for comparison */
    char buffer[32];
    size_t len = strlen(value);
    if (len >= sizeof(buffer)) {
        return default_mode; /* Value too long to be valid */
    }
    
    memcpy(buffer, value, len + 1);
    str_to_lower(buffer);
    
    /* Check for random mode values */
    if (strcmp(buffer, "random") == 0 ||
        strcmp(buffer, "rand") == 0 ||
        strcmp(buffer, "r") == 0) {
        return MEMROGUE_SAMPLING_RANDOM;
    }
    
    /* Check for deterministic mode values */
    if (strcmp(buffer, "deterministic") == 0 ||
        strcmp(buffer, "det") == 0 ||
        strcmp(buffer, "d") == 0 ||
        strcmp(buffer, "nth") == 0) {
        return MEMROGUE_SAMPLING_DETERMINISTIC;
    }
    
    /* Invalid value, use default */
    return default_mode;
}

/* ============================================================================
 * Environment Parsing Utilities
 * ============================================================================ */

bool config_parse_bool_env(const char* env_name, bool default_value) {
    if (env_name == NULL) {
        return default_value;
    }
    
    const char* value = getenv(env_name);
    if (value == NULL || *value == '\0') {
        return default_value;
    }
    
    /* Make a lowercase copy for comparison */
    char buffer[16];
    size_t len = strlen(value);
    if (len >= sizeof(buffer)) {
        return default_value; /* Value too long to be valid */
    }
    
    memcpy(buffer, value, len + 1);
    str_to_lower(buffer);
    
    /* Check for true values */
    if (strcmp(buffer, "1") == 0 ||
        strcmp(buffer, "true") == 0 ||
        strcmp(buffer, "yes") == 0 ||
        strcmp(buffer, "on") == 0) {
        return true;
    }
    
    /* Check for false values */
    if (strcmp(buffer, "0") == 0 ||
        strcmp(buffer, "false") == 0 ||
        strcmp(buffer, "no") == 0 ||
        strcmp(buffer, "off") == 0) {
        return false;
    }
    
    /* Invalid value, use default */
    return default_value;
}

int config_parse_int_env(const char* env_name, int default_value,
                         int min_value, int max_value) {
    if (env_name == NULL) {
        return default_value;
    }
    
    const char* value = getenv(env_name);
    if (value == NULL || *value == '\0') {
        return default_value;
    }
    
    /* Parse integer */
    char* endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);
    
    /* Check for parse errors */
    if (errno != 0 || endptr == value || *endptr != '\0') {
        return default_value;
    }
    
    /* Clamp to range */
    if (parsed < min_value) {
        return min_value;
    }
    if (parsed > max_value) {
        return max_value;
    }
    
    return (int)parsed;
}

bool config_parse_string_env(const char* env_name, char* buffer,
                             size_t buffer_size, const char* default_value) {
    if (env_name == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }
    
    const char* value = getenv(env_name);
    if (value == NULL || *value == '\0') {
        if (default_value != NULL) {
            size_t len = strlen(default_value);
            if (len >= buffer_size) {
                len = buffer_size - 1;
            }
            memcpy(buffer, default_value, len);
            buffer[len] = '\0';
        } else {
            buffer[0] = '\0';
        }
        return false;
    }
    
    size_t len = strlen(value);
    if (len >= buffer_size) {
        len = buffer_size - 1;
    }
    memcpy(buffer, value, len);
    buffer[len] = '\0';
    
    return true;
}

/* ============================================================================
 * Configuration Lifecycle
 * ============================================================================ */

void config_init_defaults(memrogue_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(memrogue_config_t));
    
    /* Core tracking options */
    config->enabled = true;
    config->backtrace_enabled = true;
    config->sample_rate = MEMROGUE_CONFIG_DEFAULT_SAMPLE_RATE;
    config->max_backtrace_depth = MEMROGUE_CONFIG_DEFAULT_MAX_DEPTH;
    
    /* Sampling mode (MEMRO-21) - default to random */
    config->sampling_mode = MEMROGUE_SAMPLING_RANDOM;
    
    /* Output options */
    config->output_path[0] = '\0';
    config->output_to_file = false;
    config->verbosity = MEMROGUE_VERBOSITY_NORMAL;
    
    /* Feature toggles */
    config->report_on_exit = true;
    config->detect_double_free = true;
    config->detect_invalid_free = true;
    
    /* Internal state */
    config->_initialized = true;
    config->_load_count = 0;
}

bool config_load_into(memrogue_config_t* config) {
    if (config == NULL) {
        return false;
    }
    
    /* Start with defaults */
    config_init_defaults(config);
    
    /* Parse environment variables */
    config->enabled = config_parse_bool_env(
        MEMROGUE_ENV_ENABLED, true);
    
    config->backtrace_enabled = config_parse_bool_env(
        MEMROGUE_ENV_BACKTRACE, true);
    
    config->sample_rate = config_parse_int_env(
        MEMROGUE_ENV_SAMPLE_RATE,
        MEMROGUE_CONFIG_DEFAULT_SAMPLE_RATE,
        MEMROGUE_CONFIG_MIN_SAMPLE_RATE,
        MEMROGUE_CONFIG_MAX_SAMPLE_RATE);
    
    /* Parse sampling mode (MEMRO-21) */
    config->sampling_mode = parse_sampling_mode_env(
        MEMROGUE_ENV_SAMPLING_MODE, MEMROGUE_SAMPLING_RANDOM);
    
    config->max_backtrace_depth = config_parse_int_env(
        MEMROGUE_ENV_MAX_DEPTH,
        MEMROGUE_CONFIG_DEFAULT_MAX_DEPTH,
        MEMROGUE_CONFIG_MIN_MAX_DEPTH,
        MEMROGUE_CONFIG_MAX_MAX_DEPTH);
    
    config->verbosity = (memrogue_verbosity_t)config_parse_int_env(
        MEMROGUE_ENV_VERBOSITY,
        (int)MEMROGUE_VERBOSITY_NORMAL,
        (int)MEMROGUE_VERBOSITY_QUIET,
        (int)MEMROGUE_VERBOSITY_DEBUG);
    
    config->report_on_exit = config_parse_bool_env(
        MEMROGUE_ENV_REPORT_ON_EXIT, true);
    
    config->detect_double_free = config_parse_bool_env(
        MEMROGUE_ENV_DETECT_DOUBLE_FREE, true);
    
    config->detect_invalid_free = config_parse_bool_env(
        MEMROGUE_ENV_DETECT_INVALID_FREE, true);
    
    /* Parse output path */
    bool has_output = config_parse_string_env(
        MEMROGUE_ENV_OUTPUT,
        config->output_path,
        sizeof(config->output_path),
        NULL);
    config->output_to_file = has_output && config->output_path[0] != '\0';
    
    config->_load_count++;
    
    return true;
}

const memrogue_config_t* config_load(void) {
    pthread_mutex_lock(&g_config_mutex);
    
    config_load_into(&g_config);
    atomic_store(&g_config_initialized, true);
    
    pthread_mutex_unlock(&g_config_mutex);
    
    return &g_config;
}

const memrogue_config_t* config_get(void) {
    /* Fast path: already initialized - still need mutex for safe read */
    if (atomic_load(&g_config_initialized)) {
        /* Note: We return pointer to g_config which may be modified by config_reload().
         * Callers should be aware that concurrent calls to config_reload() may
         * result in reading partially updated configuration. For truly thread-safe
         * reads during reload, use config_load_into() with a local config copy. */
        return &g_config;
    }
    
    /* Slow path: need to initialize */
    return config_load();
}

const memrogue_config_t* config_reload(void) {
    /* Lock ordering: always acquire g_config_mutex before g_output_mutex */
    pthread_mutex_lock(&g_config_mutex);
    
    /* Close existing output stream if any */
    pthread_mutex_lock(&g_output_mutex);
    if (g_output_stream != NULL && g_output_stream != stderr) {
        fclose(g_output_stream);
        g_output_stream = NULL;
    }
    pthread_mutex_unlock(&g_output_mutex);
    
    config_load_into(&g_config);
    
    pthread_mutex_unlock(&g_config_mutex);
    
    return &g_config;
}

/* ============================================================================
 * Configuration Queries
 * ============================================================================ */

bool config_is_enabled(void) {
    const memrogue_config_t* cfg = config_get();
    /* config_get() always returns valid pointer (defaults if load fails) */
    return cfg ? cfg->enabled : true;
}

bool config_backtraces_enabled(void) {
    const memrogue_config_t* cfg = config_get();
    return cfg ? cfg->backtrace_enabled : true;
}

bool config_should_sample(void) {
    const memrogue_config_t* cfg = config_get();
    if (!cfg) {
        return true; /* Default to sampling everything */
    }
    
    /* Fast path: 100% sampling */
    if (cfg->sample_rate >= 100) {
        return true;
    }
    
    /* Fast path: disabled */
    if (cfg->sample_rate <= 0) {
        return false;
    }
    
    /* MEMRO-21: Check sampling mode */
    if (cfg->sampling_mode == MEMROGUE_SAMPLING_DETERMINISTIC) {
        /* Deterministic sampling: track every Nth allocation
         * N = 100 / sample_rate
         * For sample_rate=10, N=10, so every 10th allocation is tracked
         * For sample_rate=1, N=100, so every 100th allocation is tracked
         */
        tl_sample_counter++;
        /* Defensive check against division by zero (should never happen due to config validation) */
        if (cfg->sample_rate == 0) {
            return false;
        }
        uint64_t interval = 100 / (uint64_t)cfg->sample_rate;
        return (tl_sample_counter % interval) == 0;
    }
    
    /* Random sampling based on rate (default) */
    uint32_t threshold = (uint32_t)((uint64_t)cfg->sample_rate * UINT32_MAX / 100);
    return prng_next() < threshold;
}

memrogue_sampling_mode_t config_get_sampling_mode(void) {
    const memrogue_config_t* cfg = config_get();
    return cfg ? cfg->sampling_mode : MEMROGUE_SAMPLING_RANDOM;
}

int config_get_sample_rate(void) {
    const memrogue_config_t* cfg = config_get();
    return cfg ? cfg->sample_rate : MEMROGUE_CONFIG_DEFAULT_SAMPLE_RATE;
}

void config_reset_sampling_counter(void) {
    tl_sample_counter = 0;
}

memrogue_verbosity_t config_get_verbosity(void) {
    const memrogue_config_t* cfg = config_get();
    return cfg ? cfg->verbosity : MEMROGUE_VERBOSITY_NORMAL;
}

FILE* config_get_output_stream(void) {
    bool output_to_file;
    char output_path[MEMROGUE_CONFIG_MAX_OUTPUT_PATH];
    
    /* Protect config reads with config mutex */
    pthread_mutex_lock(&g_config_mutex);
    output_to_file = g_config.output_to_file;
    strncpy(output_path, g_config.output_path, MEMROGUE_CONFIG_MAX_OUTPUT_PATH);
    output_path[MEMROGUE_CONFIG_MAX_OUTPUT_PATH - 1] = '\0'; /* Ensure null-termination */
    pthread_mutex_unlock(&g_config_mutex);
    
    /* If not outputting to file, return stderr */
    if (!output_to_file || output_path[0] == '\0') {
        return stderr;
    }
    
    pthread_mutex_lock(&g_output_mutex);
    
    /* Check if already open */
    if (g_output_stream != NULL) {
        pthread_mutex_unlock(&g_output_mutex);
        return g_output_stream;
    }
    
    /* Open the file */
    g_output_stream = fopen(output_path, "a");
    if (g_output_stream == NULL) {
        /* Fall back to stderr on error */
        pthread_mutex_unlock(&g_output_mutex);
        return stderr;
    }
    
    /* Set line buffering for timely output */
    setvbuf(g_output_stream, NULL, _IOLBF, 0);
    
    pthread_mutex_unlock(&g_output_mutex);
    return g_output_stream;
}

void config_close_output_stream(void) {
    pthread_mutex_lock(&g_output_mutex);
    
    if (g_output_stream != NULL && g_output_stream != stderr) {
        fclose(g_output_stream);
        g_output_stream = NULL;
    }
    
    pthread_mutex_unlock(&g_output_mutex);
}

/* ============================================================================
 * Debugging and Diagnostics
 * ============================================================================ */

void config_print(const memrogue_config_t* config, FILE* stream) {
    if (stream == NULL) {
        stream = stderr;
    }
    
    if (config == NULL) {
        config = config_get();
    }
    
    const char* mode_str = (config->sampling_mode == MEMROGUE_SAMPLING_DETERMINISTIC)
                           ? "deterministic" : "random";
    
    fprintf(stream, "MemRogue Configuration:\n");
    fprintf(stream, "  enabled:             %s\n", config->enabled ? "true" : "false");
    fprintf(stream, "  backtrace_enabled:   %s\n", config->backtrace_enabled ? "true" : "false");
    fprintf(stream, "  sample_rate:         %d%%\n", config->sample_rate);
    fprintf(stream, "  sampling_mode:       %s\n", mode_str);
    fprintf(stream, "  max_backtrace_depth: %d\n", config->max_backtrace_depth);
    fprintf(stream, "  output_to_file:      %s\n", config->output_to_file ? "true" : "false");
    if (config->output_to_file) {
        fprintf(stream, "  output_path:         %s\n", config->output_path);
    }
    fprintf(stream, "  verbosity:           %d\n", (int)config->verbosity);
    fprintf(stream, "  report_on_exit:      %s\n", config->report_on_exit ? "true" : "false");
    fprintf(stream, "  detect_double_free:  %s\n", config->detect_double_free ? "true" : "false");
    fprintf(stream, "  detect_invalid_free: %s\n", config->detect_invalid_free ? "true" : "false");
}

int config_to_string(const memrogue_config_t* config, char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    if (config == NULL) {
        config = config_get();
    }
    
    const char* mode_str = (config->sampling_mode == MEMROGUE_SAMPLING_DETERMINISTIC)
                           ? "deterministic" : "random";
    
    int written = snprintf(buffer, buffer_size,
        "enabled=%s, backtrace=%s, sample_rate=%d%%, sampling_mode=%s, max_depth=%d, "
        "output=%s, verbosity=%d, report_on_exit=%s, "
        "detect_double_free=%s, detect_invalid_free=%s",
        config->enabled ? "true" : "false",
        config->backtrace_enabled ? "true" : "false",
        config->sample_rate,
        mode_str,
        config->max_backtrace_depth,
        config->output_to_file ? config->output_path : "stderr",
        (int)config->verbosity,
        config->report_on_exit ? "true" : "false",
        config->detect_double_free ? "true" : "false",
        config->detect_invalid_free ? "true" : "false"
    );
    
    return written;
}
