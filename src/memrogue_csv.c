/**
 * @file memrogue_csv.c
 * @brief Implementation of CSV report formatter.
 *
 * Formats leak reports into valid CSV with proper escaping, RFC 4180
 * compliance, and configurable output options.
 *
 * Implementation Details:
 * - RFC 4180 compliant CSV output
 * - Thread-safe configuration access with mutex
 * - Buffer management for efficient string building
 * - Proper escaping of special characters (quotes, delimiters, newlines)
 * - Configurable column selection and ordering
 *
 * MEMRO-23: CSV Export Format
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "memrogue_csv.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
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
#define MAX_BUFFER_SIZE (64 * 1024 * 1024)  /* 64 MB max CSV size */
#define MAX_FIELD_SIZE 4096                  /* Maximum single field size */

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * Dynamic string buffer for building CSV.
 */
typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} csv_buffer_t;

/**
 * Internal CSV formatter structure.
 */
struct csv_formatter_internal {
    csv_config_t config;
    pthread_mutex_t config_lock;    /**< Protects config updates */
    bool initialized;
};

/* ============================================================================
 * Buffer Management
 * ============================================================================ */

/**
 * Initialize a CSV buffer.
 */
static bool buffer_init(csv_buffer_t* buf, size_t initial_capacity) {
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
 * Free a CSV buffer.
 */
static void buffer_free(csv_buffer_t* buf) {
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
static bool buffer_ensure_capacity(csv_buffer_t* buf, size_t needed) {
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
static bool buffer_append(csv_buffer_t* buf, const char* str) {
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
static bool buffer_append_char(csv_buffer_t* buf, char c) {
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
static bool buffer_appendf(csv_buffer_t* buf, const char* format, ...) {
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

/* ============================================================================
 * CSV String Utilities
 * ============================================================================ */

bool csv_field_needs_escaping(const char* input, const csv_config_t* config) {
    if (!input || !config) {
        return false;
    }
    
    const char* p = input;
    while (*p) {
        char c = *p;
        /* Field needs escaping if it contains delimiter, quote, or newline */
        if (c == config->delimiter || c == config->quote_char ||
            c == '\n' || c == '\r') {
            return true;
        }
        p++;
    }
    return false;
}

size_t csv_escape_field(const char* input, char* buffer, size_t buffer_size,
                        const csv_config_t* config) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    
    if (!input) {
        buffer[0] = '\0';
        return 0;
    }
    
    /* Use default config if none provided */
    csv_config_t default_cfg;
    if (!config) {
        csv_config_init(&default_cfg);
        config = &default_cfg;
    }
    
    bool needs_escape = config->quote_all_fields ||
                        csv_field_needs_escaping(input, config);
    
    size_t out_pos = 0;
    const char* p = input;
    
    /* Add opening quote if escaping */
    if (needs_escape && out_pos < buffer_size - 1) {
        buffer[out_pos++] = config->quote_char;
    }
    
    /* Process each character */
    while (*p && out_pos < buffer_size - 1) {
        char c = *p;
        
        if (needs_escape && c == config->quote_char) {
            /* Double the quote character */
            if (out_pos + 2 <= buffer_size - 1) {
                buffer[out_pos++] = config->quote_char;
                buffer[out_pos++] = config->quote_char;
            } else {
                break;  /* Not enough space */
            }
        } else {
            buffer[out_pos++] = c;
        }
        p++;
    }
    
    /* Add closing quote if escaping */
    if (needs_escape && out_pos < buffer_size - 1) {
        buffer[out_pos++] = config->quote_char;
    }
    
    buffer[out_pos] = '\0';
    return out_pos;
}

char* csv_escape_field_alloc(const char* input, const csv_config_t* config) {
    if (!input) {
        char* empty = malloc(1);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }
    
    /* Use default config if none provided */
    csv_config_t default_cfg;
    if (!config) {
        csv_config_init(&default_cfg);
        config = &default_cfg;
    }
    
    /* Calculate worst-case size (every char is a quote that needs doubling) */
    size_t input_len = strlen(input);
    size_t max_size = input_len * 2 + 3;  /* +3 for quotes and null terminator */
    
    char* buffer = malloc(max_size);
    if (!buffer) {
        return NULL;
    }
    
    csv_escape_field(input, buffer, max_size, config);
    return buffer;
}

char* csv_format_address(const void* ptr, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return NULL;
    }
    
    snprintf(buffer, buffer_size, "0x%016" PRIxPTR, (uintptr_t)ptr);
    return buffer;
}

char* csv_format_timestamp(uint64_t timestamp, char* buffer, size_t buffer_size) {
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
    
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &tm_info);
    return buffer;
}

/* ============================================================================
 * Configuration API
 * ============================================================================ */

void csv_config_init(csv_config_t* config) {
    if (!config) {
        return;
    }
    
    config->style = CSV_STYLE_WITH_HEADER;
    config->delimiter = ',';
    config->quote_char = '"';
    /* RFC 4180 specifies CRLF line endings */
    config->newline[0] = '\r';
    config->newline[1] = '\n';
    config->newline[2] = '\0';
    config->columns = CSV_COL_DEFAULT;
    config->quote_all_fields = false;
    config->include_summary_row = false;
    config->max_groups = 0;              /* Unlimited */
    config->max_entries_per_group = 0;   /* Unlimited */
    config->max_backtrace_depth = 10;
}

/* ============================================================================
 * Formatter Lifecycle
 * ============================================================================ */

csv_formatter_t* csv_formatter_create(void) {
    csv_config_t config;
    csv_config_init(&config);
    return csv_formatter_create_with_config(&config);
}

csv_formatter_t* csv_formatter_create_with_config(const csv_config_t* config) {
    if (!config) {
        return NULL;
    }
    
    csv_formatter_t* formatter = calloc(1, sizeof(csv_formatter_t));
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

void csv_formatter_destroy(csv_formatter_t* formatter) {
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

void csv_formatter_set_config(csv_formatter_t* formatter,
                              const csv_config_t* config) {
    if (!formatter || !formatter->initialized || !config) {
        return;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    formatter->config = *config;
    pthread_mutex_unlock(&formatter->config_lock);
}

void csv_formatter_get_config(csv_formatter_t* formatter,
                              csv_config_t* out_config) {
    if (!formatter || !formatter->initialized || !out_config) {
        return;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    *out_config = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
}

/* ============================================================================
 * Column Name Mapping
 * ============================================================================ */

const char* csv_column_name(csv_column_flags_t column) {
    switch (column) {
        case CSV_COL_ADDRESS:        return "address";
        case CSV_COL_SIZE:           return "size";
        case CSV_COL_TIMESTAMP:      return "timestamp";
        case CSV_COL_FUNCTION:       return "function";
        case CSV_COL_FILE:           return "file";
        case CSV_COL_LINE:           return "line";
        case CSV_COL_GROUP_ID:       return "group_id";
        case CSV_COL_TOTAL_IN_GROUP: return "total_in_group";
        case CSV_COL_TOTAL_BYTES:    return "total_bytes";
        case CSV_COL_BACKTRACE:      return "backtrace";
        default:                     return NULL;
    }
}

/* ============================================================================
 * Internal Formatting Helpers
 * ============================================================================ */

/**
 * Array of all individual column flags in order.
 */
static const csv_column_flags_t ALL_COLUMNS[] = {
    CSV_COL_ADDRESS,
    CSV_COL_SIZE,
    CSV_COL_TIMESTAMP,
    CSV_COL_FUNCTION,
    CSV_COL_FILE,
    CSV_COL_LINE,
    CSV_COL_GROUP_ID,
    CSV_COL_TOTAL_IN_GROUP,
    CSV_COL_TOTAL_BYTES,
    CSV_COL_BACKTRACE
};

#define NUM_COLUMNS (sizeof(ALL_COLUMNS) / sizeof(ALL_COLUMNS[0]))

/**
 * Append a field to the buffer with proper escaping.
 */
static bool csv_append_field(csv_buffer_t* buf, const char* value,
                             const csv_config_t* cfg, bool is_first) {
    /* Add delimiter before field if not first */
    if (!is_first) {
        if (!buffer_append_char(buf, cfg->delimiter)) {
            return false;
        }
    }
    
    /* Escape and append field */
    char* escaped = csv_escape_field_alloc(value, cfg);
    if (!escaped) {
        return false;
    }
    
    bool ok = buffer_append(buf, escaped);
    free(escaped);
    return ok;
}

/**
 * Append a numeric field to the buffer.
 */
static bool csv_append_number(csv_buffer_t* buf, size_t value,
                              const csv_config_t* cfg, bool is_first) {
    if (!is_first) {
        if (!buffer_append_char(buf, cfg->delimiter)) {
            return false;
        }
    }
    
    return buffer_appendf(buf, "%zu", value);
}

/**
 * Append an integer field to the buffer.
 */
static bool csv_append_int(csv_buffer_t* buf, int value,
                           const csv_config_t* cfg, bool is_first) {
    if (!is_first) {
        if (!buffer_append_char(buf, cfg->delimiter)) {
            return false;
        }
    }
    
    return buffer_appendf(buf, "%d", value);
}

/**
 * Append a uint64 field to the buffer (for signatures).
 */
static bool csv_append_uint64_hex(csv_buffer_t* buf, uint64_t value,
                                  const csv_config_t* cfg, bool is_first) {
    if (!is_first) {
        if (!buffer_append_char(buf, cfg->delimiter)) {
            return false;
        }
    }
    
    return buffer_appendf(buf, "0x%016" PRIx64, value);
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

/**
 * Get function name from first backtrace frame.
 */
static const char* get_function_from_backtrace(void* const* frames, 
                                               int frame_count,
                                               char* buffer, 
                                               size_t buffer_size) {
    if (!frames || frame_count <= 0 || buffer_size == 0) {
        if (buffer) {
            buffer[0] = '\0';
            return buffer;
        } else {
            return NULL;
        }
    }
    
    if (!buffer) {
        return NULL;
    }
    
    char** symbols = frames_to_symbols(frames, 1);
    if (symbols && symbols[0]) {
        /* Try to extract just the function name from the symbol */
        /* Format is typically: "binary(function+offset) [address]" */
        const char* sym = symbols[0];
        const char* paren = strchr(sym, '(');
        if (paren) {
            const char* plus = strchr(paren, '+');
            const char* close = strchr(paren, ')');
            if (plus && close && plus < close) {
                size_t len = (size_t)(plus - paren - 1);
                if (len >= buffer_size) {
                    len = buffer_size - 1;
                }
                memcpy(buffer, paren + 1, len);
                buffer[len] = '\0';
            } else if (close) {
                size_t len = (size_t)(close - paren - 1);
                if (len >= buffer_size) {
                    len = buffer_size - 1;
                }
                memcpy(buffer, paren + 1, len);
                buffer[len] = '\0';
            } else {
                snprintf(buffer, buffer_size, "%s", sym);
            }
        } else {
            snprintf(buffer, buffer_size, "%s", sym);
        }
        free(symbols);
    } else {
        /* Fall back to hex address */
        csv_format_address(frames[0], buffer, buffer_size);
    }
    
    return buffer;
}

/**
 * Format full backtrace as semicolon-separated string.
 */
static char* format_backtrace_csv(void* const* frames, int frame_count,
                                  size_t max_depth) {
    if (!frames || frame_count <= 0) {
        char* empty = malloc(1);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }
    
    int max_frames = frame_count;
    if (max_depth > 0 && max_frames > (int)max_depth) {
        max_frames = (int)max_depth;
    }
    
    /* Allocate buffer for backtrace string */
    size_t buf_size = (size_t)max_frames * 128;  /* Estimate 128 chars per frame */
    char* result = malloc(buf_size);
    if (!result) {
        return NULL;
    }
    result[0] = '\0';
    
    char** symbols = frames_to_symbols(frames, max_frames);
    size_t pos = 0;
    
    for (int i = 0; i < max_frames && pos < buf_size - 1; i++) {
        if (i > 0) {
            result[pos++] = ';';
        }
        
        const char* sym;
        char addr_buf[32];
        if (symbols && symbols[i]) {
            sym = symbols[i];
        } else {
            csv_format_address(frames[i], addr_buf, sizeof(addr_buf));
            sym = addr_buf;
        }
        
        size_t sym_len = strlen(sym);
        if (pos + sym_len >= buf_size - 1) {
            sym_len = buf_size - 1 - pos;
        }
        memcpy(result + pos, sym, sym_len);
        pos += sym_len;
    }
    result[pos] = '\0';
    
    free(symbols);
    return result;
}

/* ============================================================================
 * Header Generation
 * ============================================================================ */

char* csv_generate_header(csv_formatter_t* formatter) {
    if (!formatter || !formatter->initialized) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    csv_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    csv_buffer_t buf;
    if (!buffer_init(&buf, 256)) {
        return NULL;
    }
    
    bool first = true;
    for (size_t i = 0; i < NUM_COLUMNS; i++) {
        if (cfg.columns & ALL_COLUMNS[i]) {
            const char* name = csv_column_name(ALL_COLUMNS[i]);
            if (name) {
                if (!csv_append_field(&buf, name, &cfg, first)) {
                    buffer_free(&buf);
                    return NULL;
                }
                first = false;
            }
        }
    }
    
    /* Add newline */
    if (!buffer_append(&buf, cfg.newline)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

/* ============================================================================
 * Row Formatting
 * ============================================================================ */

char* csv_format_entry_row(csv_formatter_t* formatter,
                           const leak_entry_t* entry,
                           const leak_group_t* group) {
    if (!formatter || !formatter->initialized || !entry) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    csv_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    csv_buffer_t buf;
    if (!buffer_init(&buf, 512)) {
        return NULL;
    }
    
    bool first = true;
    char temp_buf[MAX_FIELD_SIZE];
    
    /* Address */
    if (cfg.columns & CSV_COL_ADDRESS) {
        csv_format_address(entry->address, temp_buf, sizeof(temp_buf));
        if (!csv_append_field(&buf, temp_buf, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Size */
    if (cfg.columns & CSV_COL_SIZE) {
        if (!csv_append_number(&buf, entry->size, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Timestamp */
    if (cfg.columns & CSV_COL_TIMESTAMP) {
        csv_format_timestamp(entry->timestamp, temp_buf, sizeof(temp_buf));
        if (!csv_append_field(&buf, temp_buf, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Function - from backtrace */
    if (cfg.columns & CSV_COL_FUNCTION) {
        get_function_from_backtrace(entry->frames, entry->frame_count,
                                    temp_buf, sizeof(temp_buf));
        if (!csv_append_field(&buf, temp_buf, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* File */
    if (cfg.columns & CSV_COL_FILE) {
        if (!csv_append_field(&buf, entry->file ? entry->file : "", &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Line */
    if (cfg.columns & CSV_COL_LINE) {
        if (!csv_append_int(&buf, entry->line, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Group ID (from group if available) */
    if (cfg.columns & CSV_COL_GROUP_ID) {
        if (group) {
            if (!csv_append_uint64_hex(&buf, group->signature, &cfg, first)) {
                buffer_free(&buf);
                return NULL;
            }
        } else {
            if (!csv_append_field(&buf, "", &cfg, first)) {
                buffer_free(&buf);
                return NULL;
            }
        }
        first = false;
    }
    
    /* Total in group */
    if (cfg.columns & CSV_COL_TOTAL_IN_GROUP) {
        if (group) {
            if (!csv_append_number(&buf, group->leak_count, &cfg, first)) {
                buffer_free(&buf);
                return NULL;
            }
        } else {
            if (!csv_append_field(&buf, "", &cfg, first)) {
                buffer_free(&buf);
                return NULL;
            }
        }
        first = false;
    }
    
    /* Total bytes in group */
    if (cfg.columns & CSV_COL_TOTAL_BYTES) {
        if (group) {
            if (!csv_append_number(&buf, group->total_bytes, &cfg, first)) {
                buffer_free(&buf);
                return NULL;
            }
        } else {
            if (!csv_append_field(&buf, "", &cfg, first)) {
                buffer_free(&buf);
                return NULL;
            }
        }
        first = false;
    }
    
    /* Backtrace */
    if (cfg.columns & CSV_COL_BACKTRACE) {
        char* bt = format_backtrace_csv(entry->frames, entry->frame_count,
                                        cfg.max_backtrace_depth);
        if (!bt) {
            buffer_free(&buf);
            return NULL;
        }
        bool ok = csv_append_field(&buf, bt, &cfg, first);
        free(bt);
        if (!ok) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Add newline */
    if (!buffer_append(&buf, cfg.newline)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

char* csv_format_summary_row(csv_formatter_t* formatter,
                             const leak_report_t* report) {
    if (!formatter || !formatter->initialized || !report) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    csv_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    csv_buffer_t buf;
    if (!buffer_init(&buf, 256)) {
        return NULL;
    }
    
    bool first = true;
    char temp_buf[64];
    
    /* For summary row, use meaningful values where appropriate */
    
    /* Address - use "TOTAL" as marker */
    if (cfg.columns & CSV_COL_ADDRESS) {
        if (!csv_append_field(&buf, "TOTAL", &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Size - total bytes */
    if (cfg.columns & CSV_COL_SIZE) {
        if (!csv_append_number(&buf, report->total_bytes, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Timestamp - current time */
    if (cfg.columns & CSV_COL_TIMESTAMP) {
        csv_format_timestamp(0, temp_buf, sizeof(temp_buf));
        if (!csv_append_field(&buf, temp_buf, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Function - severity */
    if (cfg.columns & CSV_COL_FUNCTION) {
        if (!csv_append_field(&buf, leak_severity_to_string(report->severity),
                              &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* File - empty */
    if (cfg.columns & CSV_COL_FILE) {
        if (!csv_append_field(&buf, "", &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Line - total leaks */
    if (cfg.columns & CSV_COL_LINE) {
        if (!csv_append_number(&buf, report->total_leaks, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Group ID - group count */
    if (cfg.columns & CSV_COL_GROUP_ID) {
        snprintf(temp_buf, sizeof(temp_buf), "%zu groups", report->group_count);
        if (!csv_append_field(&buf, temp_buf, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Total in group - total leaks */
    if (cfg.columns & CSV_COL_TOTAL_IN_GROUP) {
        if (!csv_append_number(&buf, report->total_leaks, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Total bytes */
    if (cfg.columns & CSV_COL_TOTAL_BYTES) {
        if (!csv_append_number(&buf, report->total_bytes, &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Backtrace - empty */
    if (cfg.columns & CSV_COL_BACKTRACE) {
        if (!csv_append_field(&buf, "", &cfg, first)) {
            buffer_free(&buf);
            return NULL;
        }
        first = false;
    }
    
    /* Add newline */
    if (!buffer_append(&buf, cfg.newline)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

/* ============================================================================
 * Main CSV Generation
 * ============================================================================ */

char* report_to_csv(csv_formatter_t* formatter, const leak_report_t* report) {
    if (!formatter || !formatter->initialized || !report) {
        return NULL;
    }
    
    /* Get config snapshot under lock */
    pthread_mutex_lock(&formatter->config_lock);
    csv_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    /* Initialize buffer */
    csv_buffer_t buf;
    if (!buffer_init(&buf, INITIAL_BUFFER_SIZE)) {
        return NULL;
    }
    
    /* Generate header if requested */
    if (cfg.style == CSV_STYLE_WITH_HEADER) {
        char* header = csv_generate_header(formatter);
        if (!header) {
            buffer_free(&buf);
            return NULL;
        }
        bool ok = buffer_append(&buf, header);
        free(header);
        if (!ok) {
            buffer_free(&buf);
            return NULL;
        }
    }
    
    /* Iterate through groups and entries */
    size_t group_count = 0;
    for (const leak_group_t* g = report->groups; g != NULL; g = g->next) {
        if (cfg.max_groups > 0 && group_count >= cfg.max_groups) {
            break;
        }
        
        size_t entry_count = 0;
        for (const leak_entry_t* e = g->entries; e != NULL; e = e->next) {
            if (cfg.max_entries_per_group > 0 &&
                entry_count >= cfg.max_entries_per_group) {
                break;
            }
            
            char* row = csv_format_entry_row(formatter, e, g);
            if (!row) {
                buffer_free(&buf);
                return NULL;
            }
            bool ok = buffer_append(&buf, row);
            free(row);
            if (!ok) {
                buffer_free(&buf);
                return NULL;
            }
            entry_count++;
        }
        group_count++;
    }
    
    /* Add summary row if requested */
    if (cfg.include_summary_row) {
        char* summary = csv_format_summary_row(formatter, report);
        if (!summary) {
            buffer_free(&buf);
            return NULL;
        }
        bool ok = buffer_append(&buf, summary);
        free(summary);
        if (!ok) {
            buffer_free(&buf);
            return NULL;
        }
    }
    
    return buf.data;
}

/* ============================================================================
 * File Output API
 * ============================================================================ */

int csv_write_to_stream(csv_formatter_t* formatter,
                        const leak_report_t* report,
                        FILE* stream) {
    if (!formatter || !report || !stream) {
        return -1;
    }
    
    char* csv = report_to_csv(formatter, report);
    if (!csv) {
        return -1;
    }
    
    size_t len = strlen(csv);
    size_t written = fwrite(csv, 1, len, stream);
    free(csv);
    
    if (written != len) {
        return -1;
    }
    
    /* Clamp to INT_MAX to prevent overflow */
    return (len > (size_t)INT_MAX) ? INT_MAX : (int)len;
}

bool csv_write_to_file(csv_formatter_t* formatter,
                       const leak_report_t* report,
                       const char* filepath) {
    if (!formatter || !report || !filepath) {
        return false;
    }
    
    FILE* file = fopen(filepath, "w");
    if (!file) {
        return false;
    }
    
    int result = csv_write_to_stream(formatter, report, file);
    fclose(file);
    
    return result >= 0;
}
