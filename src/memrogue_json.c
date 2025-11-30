/**
 * @file memrogue_json.c
 * @brief Implementation of JSON report formatter.
 *
 * Formats leak reports into valid JSON with proper escaping, schema
 * compliance, and configurable output options.
 *
 * Implementation Details:
 * - RFC 8259 compliant JSON output
 * - Thread-safe configuration access with mutex
 * - Buffer management for efficient string building
 * - Proper escaping of special characters
 * - ISO 8601 timestamp formatting
 *
 * MEMRO-22: JSON Export Format
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "memrogue_json.h"

#include <ctype.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__GLIBC__) || defined(__APPLE__)
#define HAVE_BACKTRACE 1
#include <execinfo.h>
#else
#define HAVE_BACKTRACE 0
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define INITIAL_BUFFER_SIZE 8192
#define MAX_BUFFER_SIZE (64 * 1024 * 1024)  /* 64 MB max JSON size */

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * Dynamic string buffer for building JSON.
 */
typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} json_buffer_t;

/**
 * Internal JSON formatter structure.
 */
struct json_formatter_internal {
    json_config_t config;
    pthread_mutex_t config_lock;    /**< Protects config updates */
    bool initialized;
};

/* ============================================================================
 * Buffer Management
 * ============================================================================ */

/**
 * Initialize a JSON buffer.
 */
static bool buffer_init(json_buffer_t* buf, size_t initial_capacity) {
    if (!buf) {
        return false;
    }
    buf->data = malloc(initial_capacity);
    if (!buf->data) {
        return false;
    }
    buf->data[0] = '\0';
    buf->length = 0;
    buf->capacity = initial_capacity;
    return true;
}

/**
 * Free a JSON buffer.
 */
static void buffer_free(json_buffer_t* buf) {
    if (buf) {
        free(buf->data);
        buf->data = NULL;
        buf->length = 0;
        buf->capacity = 0;
    }
}

/**
 * Ensure buffer has at least the specified capacity.
 */
static bool buffer_ensure_capacity(json_buffer_t* buf, size_t needed) {
    if (!buf || !buf->data) {
        return false;
    }
    if (buf->capacity >= needed) {
        return true;
    }
    
    size_t new_capacity = buf->capacity * 2;
    while (new_capacity < needed) {
        /* Check for overflow before doubling */
        if (new_capacity > MAX_BUFFER_SIZE / 2) {
            new_capacity = MAX_BUFFER_SIZE;
            break;
        }
        new_capacity *= 2;
    }
    
    if (new_capacity > MAX_BUFFER_SIZE) {
        if (needed <= MAX_BUFFER_SIZE) {
            new_capacity = MAX_BUFFER_SIZE;
        } else {
            return false;  /* Exceeds maximum size */
        }
    }
    
    char* new_data = realloc(buf->data, new_capacity);
    if (!new_data) {
        return false;
    }
    
    buf->data = new_data;
    buf->capacity = new_capacity;
    return true;
}

/**
 * Append a string to the buffer.
 */
static bool buffer_append(json_buffer_t* buf, const char* str) {
    if (!buf || !str) {
        return buf != NULL;  /* NULL string is OK */
    }
    
    size_t len = strlen(str);
    size_t needed = buf->length + len + 1;
    
    if (!buffer_ensure_capacity(buf, needed)) {
        return false;
    }
    
    memcpy(buf->data + buf->length, str, len + 1);
    buf->length += len;
    return true;
}

/**
 * Append a single character to the buffer.
 */
static bool buffer_append_char(json_buffer_t* buf, char c) {
    if (!buf) {
        return false;
    }
    
    size_t needed = buf->length + 2;
    if (!buffer_ensure_capacity(buf, needed)) {
        return false;
    }
    
    buf->data[buf->length++] = c;
    buf->data[buf->length] = '\0';
    return true;
}

/**
 * Append formatted string to the buffer.
 */
static bool buffer_appendf(json_buffer_t* buf, const char* format, ...) {
    if (!buf || !format) {
        return false;
    }
    
    va_list args;
    va_start(args, format);
    
    /* First, determine the required size */
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    
    if (needed < 0) {
        va_end(args);
        return false;
    }
    
    size_t total_needed = buf->length + (size_t)needed + 1;
    if (!buffer_ensure_capacity(buf, total_needed)) {
        va_end(args);
        return false;
    }
    
    vsnprintf(buf->data + buf->length, (size_t)needed + 1, format, args);
    buf->length += (size_t)needed;
    
    va_end(args);
    return true;
}

/**
 * Append repeated character for indentation.
 */
static bool buffer_indent(json_buffer_t* buf, int level, int width) {
    if (!buf || level <= 0 || width <= 0) {
        return true;
    }
    
    /* Check for overflow before multiplying */
    if ((size_t)width != 0 && (size_t)level > SIZE_MAX / (size_t)width) {
        return false;  /* Overflow would occur */
    }
    size_t total = (size_t)level * (size_t)width;
    size_t needed = buf->length + total + 1;
    
    if (!buffer_ensure_capacity(buf, needed)) {
        return false;
    }
    
    for (size_t i = 0; i < total; i++) {
        buf->data[buf->length + i] = ' ';
    }
    buf->length += total;
    buf->data[buf->length] = '\0';
    return true;
}

/* ============================================================================
 * JSON String Utilities
 * ============================================================================ */

size_t json_escape_string(const char* input, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    
    if (!input) {
        buffer[0] = '\0';
        return 0;
    }
    
    size_t out_pos = 0;
    const char* p = input;
    
    while (*p && out_pos < buffer_size - 1) {
        unsigned char c = (unsigned char)*p;
        
        switch (c) {
            case '"':
                if (out_pos + 2 <= buffer_size - 1) {
                    buffer[out_pos++] = '\\';
                    buffer[out_pos++] = '"';
                }
                break;
            case '\\':
                if (out_pos + 2 <= buffer_size - 1) {
                    buffer[out_pos++] = '\\';
                    buffer[out_pos++] = '\\';
                }
                break;
            case '\b':
                if (out_pos + 2 <= buffer_size - 1) {
                    buffer[out_pos++] = '\\';
                    buffer[out_pos++] = 'b';
                }
                break;
            case '\f':
                if (out_pos + 2 <= buffer_size - 1) {
                    buffer[out_pos++] = '\\';
                    buffer[out_pos++] = 'f';
                }
                break;
            case '\n':
                if (out_pos + 2 <= buffer_size - 1) {
                    buffer[out_pos++] = '\\';
                    buffer[out_pos++] = 'n';
                }
                break;
            case '\r':
                if (out_pos + 2 <= buffer_size - 1) {
                    buffer[out_pos++] = '\\';
                    buffer[out_pos++] = 'r';
                }
                break;
            case '\t':
                if (out_pos + 2 <= buffer_size - 1) {
                    buffer[out_pos++] = '\\';
                    buffer[out_pos++] = 't';
                }
                break;
            default:
                /* Escape control characters as \uXXXX */
                if (c < 0x20) {
                    if (out_pos + 6 <= buffer_size - 1) {
                        snprintf(buffer + out_pos, 7, "\\u%04x", c);
                        out_pos += 6;
                    }
                } else {
                    buffer[out_pos++] = (char)c;
                }
                break;
        }
        p++;
    }
    
    buffer[out_pos] = '\0';
    return out_pos;
}

char* json_escape_string_alloc(const char* input) {
    if (!input) {
        char* empty = malloc(1);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }
    
    /* Calculate worst-case size (every char escaped as \uXXXX) */
    size_t input_len = strlen(input);
    size_t max_size = input_len * 6 + 1;
    
    char* buffer = malloc(max_size);
    if (!buffer) {
        return NULL;
    }
    
    json_escape_string(input, buffer, max_size);
    return buffer;
}

char* json_format_address(const void* ptr, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return NULL;
    }
    
    snprintf(buffer, buffer_size, "0x%016" PRIxPTR, (uintptr_t)ptr);
    return buffer;
}

char* json_format_timestamp(uint64_t timestamp, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 32) {
        return NULL;
    }
    
    /* If timestamp is 0, return current time */
    time_t seconds;
    if (timestamp == 0) {
        seconds = time(NULL);
    } else {
        /* Assume timestamp is in microseconds */
        seconds = (time_t)(timestamp / 1000000);
    }
    
    struct tm tm_info;
    if (gmtime_r(&seconds, &tm_info) == NULL) {
        buffer[0] = '\0';
        return NULL;
    }
    
    strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    return buffer;
}

/* ============================================================================
 * Configuration API
 * ============================================================================ */

void json_config_init(json_config_t* config) {
    if (!config) {
        return;
    }
    
    config->style = JSON_STYLE_PRETTY;
    config->indent_width = 2;
    config->include_backtraces = true;
    config->include_addresses = true;
    config->include_timestamps = true;
    config->include_metadata = true;
    config->include_entries = true;
    config->max_groups = 0;              /* Unlimited */
    config->max_entries_per_group = 0;   /* Unlimited */
    config->max_backtrace_depth = 0;     /* Unlimited */
}

/* ============================================================================
 * Formatter Lifecycle
 * ============================================================================ */

json_formatter_t* json_formatter_create(void) {
    json_config_t config;
    json_config_init(&config);
    return json_formatter_create_with_config(&config);
}

json_formatter_t* json_formatter_create_with_config(const json_config_t* config) {
    if (!config) {
        return NULL;
    }
    
    json_formatter_t* formatter = calloc(1, sizeof(json_formatter_t));
    if (!formatter) {
        return NULL;
    }
    
    formatter->config = *config;
    
    if (pthread_mutex_init(&formatter->config_lock, NULL) != 0) {
        free(formatter);
        return NULL;
    }
    
    formatter->initialized = true;
    return formatter;
}

void json_formatter_destroy(json_formatter_t* formatter) {
    if (!formatter) {
        return;
    }
    
    pthread_mutex_destroy(&formatter->config_lock);
    formatter->initialized = false;
    free(formatter);
}

/* ============================================================================
 * Configuration Update API
 * ============================================================================ */

void json_formatter_set_config(json_formatter_t* formatter,
                               const json_config_t* config) {
    if (!formatter || !formatter->initialized || !config) {
        return;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    formatter->config = *config;
    pthread_mutex_unlock(&formatter->config_lock);
}

void json_formatter_get_config(json_formatter_t* formatter,
                               json_config_t* out_config) {
    if (!formatter || !formatter->initialized || !out_config) {
        return;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    *out_config = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
}

/* ============================================================================
 * Internal Formatting Helpers
 * ============================================================================ */

/**
 * Append a newline (if pretty mode).
 */
static bool json_newline(json_buffer_t* buf, const json_config_t* cfg) {
    if (cfg->style == JSON_STYLE_PRETTY) {
        return buffer_append_char(buf, '\n');
    }
    return true;
}

/**
 * Append a JSON string value (with quotes and escaping).
 */
static bool json_append_string(json_buffer_t* buf, const char* str) {
    if (!buffer_append_char(buf, '"')) {
        return false;
    }
    
    if (str) {
        char* escaped = json_escape_string_alloc(str);
        if (!escaped) {
            return false;
        }
        bool ok = buffer_append(buf, escaped);
        free(escaped);
        if (!ok) {
            return false;
        }
    }
    
    return buffer_append_char(buf, '"');
}

/**
 * Convert backtrace frames to symbol strings.
 */
static char** frames_to_symbols(void* const* frames, int frame_count) {
    if (!frames || frame_count <= 0) {
        return NULL;
    }
    
#if HAVE_BACKTRACE
    return backtrace_symbols((void* const*)frames, frame_count);
#else
    return NULL;
#endif
}

/* ============================================================================
 * Section Formatting
 * ============================================================================ */

/**
 * Format a backtrace as a JSON array.
 */
static bool format_backtrace_json(json_buffer_t* buf,
                                  void* const* frames,
                                  int frame_count,
                                  const json_config_t* cfg,
                                  int indent) {
    if (!frames || frame_count <= 0) {
        return buffer_append(buf, "[]");
    }
    
    int max_frames = frame_count;
    if (cfg->max_backtrace_depth > 0 && max_frames > (int)cfg->max_backtrace_depth) {
        max_frames = (int)cfg->max_backtrace_depth;
    }
    
    char** symbols = frames_to_symbols(frames, frame_count);
    
    if (!buffer_append_char(buf, '[')) {
        free(symbols);
        return false;
    }
    if (!json_newline(buf, cfg)) {
        free(symbols);
        return false;
    }
    
    for (int i = 0; i < max_frames; i++) {
        if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
            free(symbols);
            return false;
        }
        
        if (!buffer_append_char(buf, '{')) {
            free(symbols);
            return false;
        }
        
        /* Frame index */
        if (!buffer_append(buf, "\"index\":")) {
            free(symbols);
            return false;
        }
        if (!buffer_appendf(buf, "%d", i)) {
            free(symbols);
            return false;
        }
        
        /* Address */
        if (cfg->include_addresses) {
            char addr_buf[32];
            json_format_address(frames[i], addr_buf, sizeof(addr_buf));
            if (!buffer_append(buf, ",\"address\":")) {
                free(symbols);
                return false;
            }
            if (!json_append_string(buf, addr_buf)) {
                free(symbols);
                return false;
            }
        }
        
        /* Symbol */
        if (!buffer_append(buf, ",\"symbol\":")) {
            free(symbols);
            return false;
        }
        if (symbols && symbols[i]) {
            if (!json_append_string(buf, symbols[i])) {
                free(symbols);
                return false;
            }
        } else {
            char addr_buf[32];
            json_format_address(frames[i], addr_buf, sizeof(addr_buf));
            if (!json_append_string(buf, addr_buf)) {
                free(symbols);
                return false;
            }
        }
        
        if (!buffer_append_char(buf, '}')) {
            free(symbols);
            return false;
        }
        
        if (i < max_frames - 1) {
            if (!buffer_append_char(buf, ',')) {
                free(symbols);
                return false;
            }
        }
        if (!json_newline(buf, cfg)) {
            free(symbols);
            return false;
        }
    }
    
    free(symbols);
    
    if (!buffer_indent(buf, indent, cfg->indent_width)) {
        return false;
    }
    return buffer_append_char(buf, ']');
}

/**
 * Format a single leak entry as JSON object.
 */
static bool format_entry_json(json_buffer_t* buf,
                              const leak_entry_t* entry,
                              const json_config_t* cfg,
                              int indent) {
    if (!entry) {
        return buffer_append(buf, "null");
    }
    
    if (!buffer_append_char(buf, '{')) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* Address */
    if (cfg->include_addresses) {
        if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
            return false;
        }
        char addr_buf[32];
        json_format_address(entry->address, addr_buf, sizeof(addr_buf));
        if (!buffer_append(buf, "\"address\":")) {
            return false;
        }
        if (!json_append_string(buf, addr_buf)) {
            return false;
        }
        if (!buffer_append_char(buf, ',')) {
            return false;
        }
        if (!json_newline(buf, cfg)) {
            return false;
        }
    }
    
    /* Size */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_appendf(buf, "\"size\":%zu,", entry->size)) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* File */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_append(buf, "\"file\":")) {
        return false;
    }
    if (entry->file) {
        if (!json_append_string(buf, entry->file)) {
            return false;
        }
    } else {
        if (!buffer_append(buf, "null")) {
            return false;
        }
    }
    if (!buffer_append_char(buf, ',')) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* Line */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_appendf(buf, "\"line\":%d", entry->line)) {
        return false;
    }
    
    /* Timestamp */
    if (cfg->include_timestamps) {
        if (!buffer_append_char(buf, ',')) {
            return false;
        }
        if (!json_newline(buf, cfg)) {
            return false;
        }
        if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
            return false;
        }
        if (!buffer_appendf(buf, "\"timestamp\":%" PRIu64, entry->timestamp)) {
            return false;
        }
    }
    
    /* Backtrace */
    if (cfg->include_backtraces && entry->frame_count > 0) {
        if (!buffer_append_char(buf, ',')) {
            return false;
        }
        if (!json_newline(buf, cfg)) {
            return false;
        }
        if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
            return false;
        }
        if (!buffer_append(buf, "\"backtrace\":")) {
            return false;
        }
        if (!format_backtrace_json(buf, entry->frames, entry->frame_count, cfg, indent + 1)) {
            return false;
        }
    }
    
    if (!json_newline(buf, cfg)) {
        return false;
    }
    if (!buffer_indent(buf, indent, cfg->indent_width)) {
        return false;
    }
    return buffer_append_char(buf, '}');
}

/**
 * Format a leak group as JSON object.
 */
static bool format_group_json(json_buffer_t* buf,
                              const leak_group_t* group,
                              size_t group_index,
                              const json_config_t* cfg,
                              int indent) {
    if (!group) {
        return buffer_append(buf, "null");
    }
    
    if (!buffer_append_char(buf, '{')) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* Index */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_appendf(buf, "\"index\":%zu,", group_index)) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* Signature */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_appendf(buf, "\"signature\":\"0x%016" PRIx64 "\",", group->signature)) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* File */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_append(buf, "\"file\":")) {
        return false;
    }
    if (group->file) {
        if (!json_append_string(buf, group->file)) {
            return false;
        }
    } else {
        if (!buffer_append(buf, "null")) {
            return false;
        }
    }
    if (!buffer_append_char(buf, ',')) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* Line */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_appendf(buf, "\"line\":%d,", group->line)) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* Leak count */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_appendf(buf, "\"leakCount\":%zu,", group->leak_count)) {
        return false;
    }
    if (!json_newline(buf, cfg)) {
        return false;
    }
    
    /* Total bytes */
    if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
        return false;
    }
    if (!buffer_appendf(buf, "\"totalBytes\":%zu", group->total_bytes)) {
        return false;
    }
    
    /* Backtrace */
    if (cfg->include_backtraces && group->frame_count > 0) {
        if (!buffer_append_char(buf, ',')) {
            return false;
        }
        if (!json_newline(buf, cfg)) {
            return false;
        }
        if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
            return false;
        }
        if (!buffer_append(buf, "\"backtrace\":")) {
            return false;
        }
        if (!format_backtrace_json(buf, group->frames, group->frame_count, cfg, indent + 1)) {
            return false;
        }
    }
    
    /* Entries */
    if (cfg->include_entries && group->entries) {
        if (!buffer_append_char(buf, ',')) {
            return false;
        }
        if (!json_newline(buf, cfg)) {
            return false;
        }
        if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
            return false;
        }
        if (!buffer_append(buf, "\"entries\":[")) {
            return false;
        }
        if (!json_newline(buf, cfg)) {
            return false;
        }
        
        size_t entry_count = 0;
        bool first_entry = true;
        for (const leak_entry_t* e = group->entries; e != NULL; e = e->next) {
            if (cfg->max_entries_per_group > 0 && entry_count >= cfg->max_entries_per_group) {
                break;
            }
            
            if (!first_entry) {
                if (!buffer_append_char(buf, ',')) {
                    return false;
                }
                if (!json_newline(buf, cfg)) {
                    return false;
                }
            }
            first_entry = false;
            
            if (!buffer_indent(buf, indent + 2, cfg->indent_width)) {
                return false;
            }
            if (!format_entry_json(buf, e, cfg, indent + 2)) {
                return false;
            }
            entry_count++;
        }
        
        if (!json_newline(buf, cfg)) {
            return false;
        }
        if (!buffer_indent(buf, indent + 1, cfg->indent_width)) {
            return false;
        }
        if (!buffer_append_char(buf, ']')) {
            return false;
        }
    }
    
    if (!json_newline(buf, cfg)) {
        return false;
    }
    if (!buffer_indent(buf, indent, cfg->indent_width)) {
        return false;
    }
    return buffer_append_char(buf, '}');
}

/* ============================================================================
 * Main JSON Generation
 * ============================================================================ */

char* report_to_json(json_formatter_t* formatter, const leak_report_t* report) {
    if (!formatter || !formatter->initialized || !report) {
        return NULL;
    }
    
    /* Get config snapshot under lock */
    pthread_mutex_lock(&formatter->config_lock);
    json_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    /* Initialize buffer */
    json_buffer_t buf;
    if (!buffer_init(&buf, INITIAL_BUFFER_SIZE)) {
        return NULL;
    }
    
    /* Root object start */
    if (!buffer_append_char(&buf, '{')) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Schema version */
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append(&buf, "\"version\":")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_append_string(&buf, MEMROGUE_JSON_SCHEMA_VERSION)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append_char(&buf, ',')) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Generator */
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append(&buf, "\"generator\":\"memrogue\",")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Timestamp */
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    char ts_buf[64];
    json_format_timestamp(0, ts_buf, sizeof(ts_buf));  /* Current time */
    if (!buffer_append(&buf, "\"timestamp\":")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_append_string(&buf, ts_buf)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append_char(&buf, ',')) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Summary section */
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append(&buf, "\"summary\":{")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Summary fields */
    if (!buffer_indent(&buf, 2, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_appendf(&buf, "\"totalLeaks\":%zu,", report->total_leaks)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 2, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_appendf(&buf, "\"totalBytes\":%zu,", report->total_bytes)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 2, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_appendf(&buf, "\"groupCount\":%zu,", report->group_count)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 2, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append(&buf, "\"severity\":")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_append_string(&buf, leak_severity_to_string(report->severity))) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append_char(&buf, ',')) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 2, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_appendf(&buf, "\"hasLeaks\":%s", 
                        leak_report_has_leaks(report) ? "true" : "false")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append(&buf, "},")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Groups array */
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append(&buf, "\"groups\":[")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Format groups */
    size_t group_count = 0;
    bool first_group = true;
    for (const leak_group_t* g = report->groups; g != NULL; g = g->next) {
        if (cfg.max_groups > 0 && group_count >= cfg.max_groups) {
            break;
        }
        
        if (!first_group) {
            if (!buffer_append_char(&buf, ',')) {
                buffer_free(&buf);
                return NULL;
            }
            if (!json_newline(&buf, &cfg)) {
                buffer_free(&buf);
                return NULL;
            }
        }
        first_group = false;
        
        if (!buffer_indent(&buf, 2, cfg.indent_width)) {
            buffer_free(&buf);
            return NULL;
        }
        if (!format_group_json(&buf, g, group_count, &cfg, 2)) {
            buffer_free(&buf);
            return NULL;
        }
        group_count++;
    }
    
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append_char(&buf, ']')) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Metadata section */
    if (cfg.include_metadata) {
        if (!buffer_append_char(&buf, ',')) {
            buffer_free(&buf);
            return NULL;
        }
        if (!json_newline(&buf, &cfg)) {
            buffer_free(&buf);
            return NULL;
        }
        
        if (!buffer_indent(&buf, 1, cfg.indent_width)) {
            buffer_free(&buf);
            return NULL;
        }
        if (!buffer_append(&buf, "\"metadata\":{")) {
            buffer_free(&buf);
            return NULL;
        }
        if (!json_newline(&buf, &cfg)) {
            buffer_free(&buf);
            return NULL;
        }
        
        if (!buffer_indent(&buf, 2, cfg.indent_width)) {
            buffer_free(&buf);
            return NULL;
        }
        if (!buffer_appendf(&buf, "\"detectionTimeUs\":%" PRIu64 ",", 
                           report->detection_time_us)) {
            buffer_free(&buf);
            return NULL;
        }
        if (!json_newline(&buf, &cfg)) {
            buffer_free(&buf);
            return NULL;
        }
        
        if (!buffer_indent(&buf, 2, cfg.indent_width)) {
            buffer_free(&buf);
            return NULL;
        }
        if (!buffer_appendf(&buf, "\"suppressionApplied\":%s",
                           report->suppression_applied ? "true" : "false")) {
            buffer_free(&buf);
            return NULL;
        }
        if (!json_newline(&buf, &cfg)) {
            buffer_free(&buf);
            return NULL;
        }
        
        if (!buffer_indent(&buf, 1, cfg.indent_width)) {
            buffer_free(&buf);
            return NULL;
        }
        if (!buffer_append_char(&buf, '}')) {
            buffer_free(&buf);
            return NULL;
        }
    }
    
    /* Root object end */
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append_char(&buf, '}')) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

/* ============================================================================
 * File Output API
 * ============================================================================ */

int json_write_to_stream(json_formatter_t* formatter,
                         const leak_report_t* report,
                         FILE* stream) {
    if (!formatter || !report || !stream) {
        return -1;
    }
    
    char* json = report_to_json(formatter, report);
    if (!json) {
        return -1;
    }
    
    int written = fprintf(stream, "%s", json);
    free(json);
    
    return written;
}

bool json_write_to_file(json_formatter_t* formatter,
                        const leak_report_t* report,
                        const char* filepath) {
    if (!formatter || !report || !filepath) {
        return false;
    }
    
    FILE* file = fopen(filepath, "w");
    if (!file) {
        return false;
    }
    
    int result = json_write_to_stream(formatter, report, file);
    fclose(file);
    
    return result >= 0;
}

/* ============================================================================
 * Section Formatting API (Public)
 * ============================================================================ */

char* json_format_summary(json_formatter_t* formatter, const leak_report_t* report) {
    if (!formatter || !formatter->initialized || !report) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    json_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    json_buffer_t buf;
    if (!buffer_init(&buf, 1024)) {
        return NULL;
    }
    
    if (!buffer_append_char(&buf, '{')) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_appendf(&buf, "\"totalLeaks\":%zu,", report->total_leaks)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_appendf(&buf, "\"totalBytes\":%zu,", report->total_bytes)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_appendf(&buf, "\"groupCount\":%zu,", report->group_count)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_indent(&buf, 1, cfg.indent_width)) {
        buffer_free(&buf);
        return NULL;
    }
    if (!buffer_append(&buf, "\"severity\":")) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_append_string(&buf, leak_severity_to_string(report->severity))) {
        buffer_free(&buf);
        return NULL;
    }
    if (!json_newline(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    if (!buffer_append_char(&buf, '}')) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

char* json_format_group(json_formatter_t* formatter,
                        const leak_group_t* group,
                        size_t group_index) {
    if (!formatter || !formatter->initialized || !group) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    json_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    json_buffer_t buf;
    if (!buffer_init(&buf, 2048)) {
        return NULL;
    }
    
    if (!format_group_json(&buf, group, group_index, &cfg, 0)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

char* json_format_entry(json_formatter_t* formatter, const leak_entry_t* entry) {
    if (!formatter || !formatter->initialized || !entry) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    json_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    json_buffer_t buf;
    if (!buffer_init(&buf, 1024)) {
        return NULL;
    }
    
    if (!format_entry_json(&buf, entry, &cfg, 0)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}
