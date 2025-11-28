/**
 * @file memrogue_report.c
 * @brief Implementation of text report formatter.
 *
 * Formats leak reports into human-readable text with summary statistics,
 * grouped leaks with backtraces, sorted by size or count.
 *
 * Implementation Details:
 * - Sorts groups before formatting for deterministic output
 * - Thread-safe configuration access with mutex
 * - Buffer management for efficient string building
 * - Percentage calculation for relative leak sizes
 *
 * MEMRO-17: Text Report Formatter
 */

/* Ensure POSIX features are available (must be before any includes) */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "memrogue_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>

#if defined(__GLIBC__) || defined(__APPLE__)
#define HAVE_BACKTRACE 1
#include <execinfo.h>
#else
#define HAVE_BACKTRACE 0
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define INITIAL_BUFFER_SIZE 4096
#define MAX_BUFFER_SIZE (16 * 1024 * 1024)  /* 16 MB max report size */

/* ANSI Color codes */
#define COLOR_RESET     "\033[0m"
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_BLUE      "\033[34m"
#define COLOR_MAGENTA   "\033[35m"
#define COLOR_CYAN      "\033[36m"
#define COLOR_BOLD      "\033[1m"

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * Dynamic string buffer for building reports.
 */
typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} string_buffer_t;

/**
 * Internal report formatter structure.
 */
struct report_formatter_internal {
    report_config_t config;
    pthread_mutex_t config_lock;    /**< Protects config updates */
    bool initialized;
};

/* ============================================================================
 * String Buffer Management
 * ============================================================================ */

/**
 * Initialize a string buffer.
 */
static bool buffer_init(string_buffer_t* buf, size_t initial_capacity) {
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
 * Free a string buffer.
 */
static void buffer_free(string_buffer_t* buf) {
    free(buf->data);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

/**
 * Ensure buffer has at least the specified capacity.
 */
static bool buffer_ensure_capacity(string_buffer_t* buf, size_t needed) {
    if (buf->capacity >= needed) {
        return true;
    }
    
    size_t new_capacity = buf->capacity * 2;
    while (new_capacity < needed) {
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
static bool buffer_append(string_buffer_t* buf, const char* str) {
    if (!str) {
        return true;
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
 * Append formatted string to the buffer.
 */
static bool buffer_appendf(string_buffer_t* buf, const char* format, ...) {
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
 * Append repeated character (for indentation/separators).
 */
static bool buffer_append_repeat(string_buffer_t* buf, char c, size_t count) {
    size_t needed = buf->length + count + 1;
    if (!buffer_ensure_capacity(buf, needed)) {
        return false;
    }
    
    for (size_t i = 0; i < count; i++) {
        buf->data[buf->length + i] = c;
    }
    buf->length += count;
    buf->data[buf->length] = '\0';
    return true;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

char* format_bytes(size_t bytes, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 16) {
        return NULL;
    }
    
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    
    if (unit_index == 0) {
        snprintf(buffer, buffer_size, "%zu %s", bytes, units[unit_index]);
    } else {
        snprintf(buffer, buffer_size, "%.2f %s", size, units[unit_index]);
    }
    
    return buffer;
}

const char* report_sort_order_to_string(report_sort_order_t order) {
    switch (order) {
        case REPORT_SORT_BY_SIZE:
            return "by size (largest first)";
        case REPORT_SORT_BY_COUNT:
            return "by count (most frequent first)";
        case REPORT_SORT_BY_LOCATION:
            return "by location (file:line)";
        default:
            return "unknown";
    }
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
 * Configuration API
 * ============================================================================ */

void report_config_init(report_config_t* config) {
    if (!config) {
        return;
    }
    
    config->show_summary = true;
    config->show_backtraces = true;
    config->show_percentages = true;
    config->show_timestamps = false;
    config->show_addresses = true;
    config->use_colors = false;
    config->sort_order = REPORT_SORT_BY_SIZE;
    config->max_groups = 0;  /* Unlimited */
    config->max_entries_per_group = 5;
    config->max_backtrace_depth = 10;
    config->indent_width = 2;
}

/* ============================================================================
 * Formatter Lifecycle
 * ============================================================================ */

report_formatter_t* report_formatter_create(void) {
    report_config_t config;
    report_config_init(&config);
    return report_formatter_create_with_config(&config);
}

report_formatter_t* report_formatter_create_with_config(const report_config_t* config) {
    if (!config) {
        return NULL;
    }
    
    report_formatter_t* formatter = calloc(1, sizeof(report_formatter_t));
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

void report_formatter_destroy(report_formatter_t* formatter) {
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

void report_formatter_set_config(report_formatter_t* formatter,
                                 const report_config_t* config) {
    if (!formatter || !formatter->initialized || !config) {
        return;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    formatter->config = *config;
    pthread_mutex_unlock(&formatter->config_lock);
}

void report_formatter_get_config(report_formatter_t* formatter,
                                 report_config_t* out_config) {
    if (!formatter || !formatter->initialized || !out_config) {
        return;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    *out_config = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
}

/* ============================================================================
 * Group Sorting
 * ============================================================================ */

/**
 * Sorted group entry for report generation.
 */
typedef struct {
    const leak_group_t* group;
    size_t rank;
} sorted_group_t;

/**
 * Compare groups by total bytes (descending).
 */
static int compare_by_size(const void* a, const void* b) {
    const sorted_group_t* ga = (const sorted_group_t*)a;
    const sorted_group_t* gb = (const sorted_group_t*)b;
    
    if (ga->group->total_bytes > gb->group->total_bytes) return -1;
    if (ga->group->total_bytes < gb->group->total_bytes) return 1;
    return 0;
}

/**
 * Compare groups by leak count (descending).
 */
static int compare_by_count(const void* a, const void* b) {
    const sorted_group_t* ga = (const sorted_group_t*)a;
    const sorted_group_t* gb = (const sorted_group_t*)b;
    
    if (ga->group->leak_count > gb->group->leak_count) return -1;
    if (ga->group->leak_count < gb->group->leak_count) return 1;
    return 0;
}

/**
 * Compare groups by location (file:line).
 */
static int compare_by_location(const void* a, const void* b) {
    const sorted_group_t* ga = (const sorted_group_t*)a;
    const sorted_group_t* gb = (const sorted_group_t*)b;
    
    const char* file_a = ga->group->file ? ga->group->file : "";
    const char* file_b = gb->group->file ? gb->group->file : "";
    
    int cmp = strcmp(file_a, file_b);
    if (cmp != 0) return cmp;
    
    /* Use safe comparison to avoid integer overflow */
    if (ga->group->line < gb->group->line) return -1;
    if (ga->group->line > gb->group->line) return 1;
    return 0;
}

/**
 * Create a sorted array of groups.
 */
static sorted_group_t* sort_groups(const leak_report_t* report,
                                   report_sort_order_t order,
                                   size_t* out_count) {
    if (!report || report->group_count == 0) {
        *out_count = 0;
        return NULL;
    }
    
    sorted_group_t* sorted = malloc(report->group_count * sizeof(sorted_group_t));
    if (!sorted) {
        *out_count = 0;
        return NULL;
    }
    
    /* Populate array */
    size_t i = 0;
    for (const leak_group_t* g = report->groups; g != NULL; g = g->next) {
        sorted[i].group = g;
        sorted[i].rank = i + 1;
        i++;
    }
    
    /* Sort based on order */
    int (*compare_func)(const void*, const void*) = compare_by_size;
    switch (order) {
        case REPORT_SORT_BY_COUNT:
            compare_func = compare_by_count;
            break;
        case REPORT_SORT_BY_LOCATION:
            compare_func = compare_by_location;
            break;
        default:
            compare_func = compare_by_size;
            break;
    }
    
    qsort(sorted, report->group_count, sizeof(sorted_group_t), compare_func);
    
    /* Update ranks after sorting */
    for (i = 0; i < report->group_count; i++) {
        sorted[i].rank = i + 1;
    }
    
    *out_count = report->group_count;
    return sorted;
}

/* ============================================================================
 * Section Formatting
 * ============================================================================ */

/**
 * Format the report header.
 */
static bool format_header(string_buffer_t* buf, const report_config_t* cfg) {
    if (cfg->use_colors) {
        buffer_append(buf, COLOR_BOLD COLOR_CYAN);
    }
    
    buffer_append(buf, "================================================================================\n");
    buffer_append(buf, "                         MEMROGUE MEMORY LEAK REPORT\n");
    buffer_append(buf, "================================================================================\n");
    
    if (cfg->use_colors) {
        buffer_append(buf, COLOR_RESET);
    }
    
    return true;
}

/**
 * Format the summary section.
 */
static bool format_summary_section(string_buffer_t* buf,
                                   const leak_report_t* report,
                                   const report_config_t* cfg) {
    char size_buf[32];
    
    if (cfg->use_colors) {
        buffer_append(buf, COLOR_BOLD);
    }
    buffer_append(buf, "\n--- SUMMARY ---\n\n");
    if (cfg->use_colors) {
        buffer_append(buf, COLOR_RESET);
    }
    
    /* Severity */
    const char* severity_str = leak_severity_to_string(report->severity);
    if (cfg->use_colors) {
        const char* color = COLOR_GREEN;
        switch (report->severity) {
            case LEAK_SEVERITY_LOW:
                color = COLOR_YELLOW;
                break;
            case LEAK_SEVERITY_MEDIUM:
                color = COLOR_YELLOW;
                break;
            case LEAK_SEVERITY_HIGH:
                color = COLOR_RED;
                break;
            case LEAK_SEVERITY_CRITICAL:
                color = COLOR_BOLD COLOR_RED;
                break;
            default:
                color = COLOR_GREEN;
                break;
        }
        buffer_appendf(buf, "  Severity:      %s%s%s\n", color, severity_str, COLOR_RESET);
    } else {
        buffer_appendf(buf, "  Severity:      %s\n", severity_str);
    }
    
    /* Total leaks */
    buffer_appendf(buf, "  Total Leaks:   %zu allocation%s\n",
                   report->total_leaks,
                   report->total_leaks == 1 ? "" : "s");
    
    /* Total bytes */
    format_bytes(report->total_bytes, size_buf, sizeof(size_buf));
    buffer_appendf(buf, "  Total Bytes:   %s (%zu bytes)\n", size_buf, report->total_bytes);
    
    /* Unique locations */
    buffer_appendf(buf, "  Leak Groups:   %zu unique allocation site%s\n",
                   report->group_count,
                   report->group_count == 1 ? "" : "s");
    
    /* Detection time */
    if (report->detection_time_us > 0) {
        double ms = (double)report->detection_time_us / 1000.0;
        buffer_appendf(buf, "  Scan Time:     %.2f ms\n", ms);
    }
    
    buffer_append(buf, "\n");
    
    return true;
}

/**
 * Format a single leak entry.
 */
static bool format_entry(string_buffer_t* buf,
                         const leak_entry_t* entry,
                         const report_config_t* cfg,
                         int indent_level) {
    char size_buf[32];
    int indent = indent_level * cfg->indent_width;
    
    format_bytes(entry->size, size_buf, sizeof(size_buf));
    
    buffer_append_repeat(buf, ' ', (size_t)indent);
    
    if (cfg->show_addresses) {
        buffer_appendf(buf, "- %p: %s", entry->address, size_buf);
    } else {
        buffer_appendf(buf, "- %s", size_buf);
    }
    
    if (entry->file) {
        buffer_appendf(buf, " at %s:%d", entry->file, entry->line);
    }
    
    if (cfg->show_timestamps && entry->timestamp > 0) {
        buffer_appendf(buf, " (t=%" PRIu64 ")", entry->timestamp);
    }
    
    buffer_append(buf, "\n");
    
    return true;
}

/**
 * Format a backtrace.
 */
static bool format_backtrace_internal(string_buffer_t* buf,
                                      void* const* frames,
                                      int frame_count,
                                      const report_config_t* cfg,
                                      int indent_level) {
    if (!frames || frame_count <= 0) {
        return true;
    }
    
    int max_frames = frame_count;
    if (cfg->max_backtrace_depth > 0 && max_frames > (int)cfg->max_backtrace_depth) {
        max_frames = (int)cfg->max_backtrace_depth;
    }
    
    int indent = indent_level * cfg->indent_width;
    
    char** symbols = frames_to_symbols(frames, frame_count);
    
    for (int i = 0; i < max_frames; i++) {
        buffer_append_repeat(buf, ' ', (size_t)indent);
        
        if (cfg->use_colors) {
            buffer_append(buf, COLOR_CYAN);
        }
        buffer_appendf(buf, "#%-2d ", i);
        if (cfg->use_colors) {
            buffer_append(buf, COLOR_RESET);
        }
        
        if (symbols && symbols[i]) {
            buffer_appendf(buf, "%s\n", symbols[i]);
        } else {
            buffer_appendf(buf, "%p\n", frames[i]);
        }
    }
    
    if (frame_count > max_frames) {
        buffer_append_repeat(buf, ' ', (size_t)indent);
        buffer_appendf(buf, "... and %d more frames\n", frame_count - max_frames);
    }
    
    free(symbols);
    
    return true;
}

/**
 * Format a single leak group.
 */
static bool format_group_internal(string_buffer_t* buf,
                                  const leak_group_t* group,
                                  size_t total_bytes,
                                  size_t group_rank,
                                  const report_config_t* cfg) {
    char size_buf[32];
    int indent = cfg->indent_width;
    
    /* Group header */
    if (cfg->use_colors) {
        buffer_append(buf, COLOR_BOLD COLOR_YELLOW);
    }
    buffer_appendf(buf, "Leak Group #%zu", group_rank);
    if (cfg->use_colors) {
        buffer_append(buf, COLOR_RESET);
    }
    
    /* Location */
    if (group->file) {
        buffer_appendf(buf, " - %s:%d", group->file, group->line);
    }
    buffer_append(buf, "\n");
    
    /* Separator */
    buffer_append_repeat(buf, '-', 60);
    buffer_append(buf, "\n");
    
    /* Statistics */
    format_bytes(group->total_bytes, size_buf, sizeof(size_buf));
    buffer_append_repeat(buf, ' ', (size_t)indent);
    buffer_appendf(buf, "Count: %zu allocation%s\n",
                   group->leak_count,
                   group->leak_count == 1 ? "" : "s");
    
    buffer_append_repeat(buf, ' ', (size_t)indent);
    buffer_appendf(buf, "Total: %s (%zu bytes)", size_buf, group->total_bytes);
    
    /* Percentage */
    if (cfg->show_percentages && total_bytes > 0) {
        double pct = 100.0 * (double)group->total_bytes / (double)total_bytes;
        buffer_appendf(buf, " (%.1f%%)", pct);
    }
    buffer_append(buf, "\n");
    
    /* Backtrace */
    if (cfg->show_backtraces && group->frame_count > 0) {
        buffer_append(buf, "\n");
        buffer_append_repeat(buf, ' ', (size_t)indent);
        buffer_append(buf, "Backtrace:\n");
        format_backtrace_internal(buf, group->frames, group->frame_count, cfg, 2);
    }
    
    /* Individual entries */
    if (group->entries) {
        buffer_append(buf, "\n");
        buffer_append_repeat(buf, ' ', (size_t)indent);
        buffer_append(buf, "Leak instances:\n");
        
        size_t entry_count = 0;
        for (const leak_entry_t* e = group->entries; e != NULL; e = e->next) {
            if (cfg->max_entries_per_group > 0 && entry_count >= cfg->max_entries_per_group) {
                buffer_append_repeat(buf, ' ', (size_t)(indent * 2));
                buffer_appendf(buf, "... and %zu more\n",
                               group->leak_count - entry_count);
                break;
            }
            format_entry(buf, e, cfg, 2);
            entry_count++;
        }
    }
    
    buffer_append(buf, "\n");
    
    return true;
}

/**
 * Format the footer section.
 */
static bool format_footer(string_buffer_t* buf, const report_config_t* cfg) {
    if (cfg->use_colors) {
        buffer_append(buf, COLOR_BOLD COLOR_CYAN);
    }
    buffer_append(buf, "================================================================================\n");
    buffer_append(buf, "                              END OF LEAK REPORT\n");
    buffer_append(buf, "================================================================================\n");
    if (cfg->use_colors) {
        buffer_append(buf, COLOR_RESET);
    }
    
    return true;
}

/* ============================================================================
 * Report Generation API
 * ============================================================================ */

char* report_format_text(report_formatter_t* formatter, const leak_report_t* report) {
    if (!formatter || !formatter->initialized || !report) {
        return NULL;
    }
    
    /* Get config snapshot under lock */
    pthread_mutex_lock(&formatter->config_lock);
    report_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    /* Initialize buffer */
    string_buffer_t buf;
    if (!buffer_init(&buf, INITIAL_BUFFER_SIZE)) {
        return NULL;
    }
    
    /* Format header */
    if (!format_header(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    /* Format summary */
    if (cfg.show_summary) {
        if (!format_summary_section(&buf, report, &cfg)) {
            buffer_free(&buf);
            return NULL;
        }
    }
    
    /* Check for no leaks */
    if (!leak_report_has_leaks(report)) {
        if (cfg.use_colors) {
            if (!buffer_append(&buf, COLOR_GREEN)) {
                buffer_free(&buf);
                return NULL;
            }
        }
        if (!buffer_append(&buf, "No memory leaks detected!\n\n")) {
            buffer_free(&buf);
            return NULL;
        }
        if (cfg.use_colors) {
            if (!buffer_append(&buf, COLOR_RESET)) {
                buffer_free(&buf);
                return NULL;
            }
        }
        if (!format_footer(&buf, &cfg)) {
            buffer_free(&buf);
            return NULL;
        }
        return buf.data;
    }
    
    /* Sort groups */
    size_t sorted_count;
    sorted_group_t* sorted = sort_groups(report, cfg.sort_order, &sorted_count);
    
    if (sorted) {
        /* Section header */
        if (cfg.use_colors) {
            if (!buffer_append(&buf, COLOR_BOLD)) {
                free(sorted);
                buffer_free(&buf);
                return NULL;
            }
        }
        if (!buffer_append(&buf, "--- LEAK DETAILS ")) {
            free(sorted);
            buffer_free(&buf);
            return NULL;
        }
        if (!buffer_appendf(&buf, "(sorted %s) ---\n\n", report_sort_order_to_string(cfg.sort_order))) {
            free(sorted);
            buffer_free(&buf);
            return NULL;
        }
        if (cfg.use_colors) {
            if (!buffer_append(&buf, COLOR_RESET)) {
                free(sorted);
                buffer_free(&buf);
                return NULL;
            }
        }
        
        /* Format each group */
        size_t groups_to_show = sorted_count;
        if (cfg.max_groups > 0 && groups_to_show > cfg.max_groups) {
            groups_to_show = cfg.max_groups;
        }
        
        for (size_t i = 0; i < groups_to_show; i++) {
            if (!format_group_internal(&buf, sorted[i].group, report->total_bytes,
                                      sorted[i].rank, &cfg)) {
                free(sorted);
                buffer_free(&buf);
                return NULL;
            }
        }
        
        if (sorted_count > groups_to_show) {
            if (!buffer_appendf(&buf, "... and %zu more leak group%s (use max_groups to see more)\n\n",
                               sorted_count - groups_to_show,
                               (sorted_count - groups_to_show) == 1 ? "" : "s")) {
                free(sorted);
                buffer_free(&buf);
                return NULL;
            }
        }
        
        free(sorted);
    }
    
    /* Format footer */
    if (!format_footer(&buf, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

int report_write_to_stream(report_formatter_t* formatter,
                           const leak_report_t* report,
                           FILE* stream) {
    if (!formatter || !report || !stream) {
        return -1;
    }
    
    char* text = report_format_text(formatter, report);
    if (!text) {
        return -1;
    }
    
    int written = fprintf(stream, "%s", text);
    free(text);
    
    return written;
}

bool report_write_to_file(report_formatter_t* formatter,
                          const leak_report_t* report,
                          const char* filepath) {
    if (!formatter || !report || !filepath) {
        return false;
    }
    
    FILE* file = fopen(filepath, "w");
    if (!file) {
        return false;
    }
    
    int result = report_write_to_stream(formatter, report, file);
    fclose(file);
    
    return result >= 0;
}

/* ============================================================================
 * Section Formatting API (Public)
 * ============================================================================ */

char* report_format_summary(report_formatter_t* formatter, const leak_report_t* report) {
    if (!formatter || !formatter->initialized || !report) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    report_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    string_buffer_t buf;
    if (!buffer_init(&buf, 1024)) {
        return NULL;
    }
    
    if (!format_summary_section(&buf, report, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

char* report_format_group(report_formatter_t* formatter,
                          const leak_group_t* group,
                          size_t total_bytes,
                          size_t group_rank) {
    if (!formatter || !formatter->initialized || !group) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    report_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    string_buffer_t buf;
    if (!buffer_init(&buf, 2048)) {
        return NULL;
    }
    
    if (!format_group_internal(&buf, group, total_bytes, group_rank, &cfg)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}

char* report_format_backtrace(report_formatter_t* formatter,
                              void* const* frames,
                              int frame_count) {
    if (!formatter || !formatter->initialized || !frames || frame_count <= 0) {
        return NULL;
    }
    
    pthread_mutex_lock(&formatter->config_lock);
    report_config_t cfg = formatter->config;
    pthread_mutex_unlock(&formatter->config_lock);
    
    string_buffer_t buf;
    if (!buffer_init(&buf, 1024)) {
        return NULL;
    }
    
    if (!format_backtrace_internal(&buf, frames, frame_count, &cfg, 0)) {
        buffer_free(&buf);
        return NULL;
    }
    
    return buf.data;
}
