#include "memrogue_backtrace.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#if defined(__GLIBC__) || defined(__APPLE__)
#define HAVE_BACKTRACE 1
#include <execinfo.h>
#else
#define HAVE_BACKTRACE 0
#endif

int backtrace_available(void) {
#if HAVE_BACKTRACE
    return 1;
#else
    return 0;
#endif
}

int backtrace_capture(allocation_info_t* info, int skip_frames) {
    if (!info) {
        return 0;
    }
    
    // Initialize to empty state
    info->frame_count = 0;
    memset(info->frames, 0, sizeof(info->frames));

#if HAVE_BACKTRACE
    // We need extra space for frames we'll skip
    void* temp_buffer[MEMROGUE_MAX_FRAMES + 16];
    int max_capture = MEMROGUE_MAX_FRAMES + skip_frames;
    
    if (max_capture > (int)(sizeof(temp_buffer) / sizeof(temp_buffer[0]))) {
        max_capture = (int)(sizeof(temp_buffer) / sizeof(temp_buffer[0]));
    }
    
    // Capture the backtrace
    int total_frames = backtrace(temp_buffer, max_capture);
    
    if (total_frames <= 0) {
        // backtrace() failed
        return 0;
    }
    
    // Calculate how many frames to copy after skipping
    int frames_to_copy = total_frames - skip_frames;
    
    if (frames_to_copy <= 0) {
        // All frames were skipped
        return 0;
    }
    
    if (frames_to_copy > MEMROGUE_MAX_FRAMES) {
        frames_to_copy = MEMROGUE_MAX_FRAMES;
    }
    
    // Copy frames, skipping the first 'skip_frames'
    for (int i = 0; i < frames_to_copy; i++) {
        info->frames[i] = temp_buffer[skip_frames + i];
    }
    
    info->frame_count = frames_to_copy;
    return frames_to_copy;
    
#else
    // Backtrace not available on this platform
    (void)skip_frames;
    return 0;
#endif
}

/**
 * Parse a backtrace_symbols() output string.
 * Format varies by platform:
 * - Linux/glibc: "./program(function+0x1a) [0x400a1a]" or "./program [0x400a1a]"
 * - macOS: "0   program    0x00007fff5fbff8c0 function + 16"
 */
static void parse_symbol_string(const char* symbol_str, resolved_frame_t* frame) {
    if (!symbol_str || !frame) return;
    
    // Copy the full symbol string
    strncpy(frame->symbol, symbol_str, MEMROGUE_MAX_SYMBOL_LEN - 1);
    frame->symbol[MEMROGUE_MAX_SYMBOL_LEN - 1] = '\0';
    
    // Initialize parsed fields
    frame->function_name = NULL;
    frame->file_name = NULL;
    frame->line_number = 0;
    frame->offset = -1;
    
#if defined(__GLIBC__)
    // Linux format: "path(function+0xoffset) [0xaddress]"
    // or: "path [0xaddress]" (no symbol)
    
    char* open_paren = strchr(frame->symbol, '(');
    char* close_paren = strchr(frame->symbol, ')');
    
    if (open_paren && close_paren && close_paren > open_paren) {
        // We have a function name
        *open_paren = '\0';
        frame->file_name = frame->symbol;
        
        char* func_start = open_paren + 1;
        char* plus_sign = strchr(func_start, '+');
        
        if (plus_sign && plus_sign < close_paren) {
            *plus_sign = '\0';
            if (func_start[0] != '\0') {
                frame->function_name = func_start;
            }
            
            // Parse offset (hex)
            char* offset_str = plus_sign + 1;
            if (offset_str[0] != '\0' && offset_str[1] != '\0' && offset_str[0] == '0' && offset_str[1] == 'x') {
                frame->offset = (int)strtol(offset_str, NULL, 16);
            }
        } else {
            // No offset, just function name
            *close_paren = '\0';
            if (func_start[0] != '\0') {
                frame->function_name = func_start;
            }
        }
    } else {
        // No symbol info, just show the path/address
        char* bracket = strchr(frame->symbol, '[');
        if (bracket && bracket > frame->symbol) {
            *(bracket - 1) = '\0';
            frame->file_name = frame->symbol;
        }
    }
    
#elif defined(__APPLE__)
    // macOS format: "index  binary  address function + offset"
    // Example: "0   test_backtrace  0x100003f20 main + 16"
    
    // Skip the index number
    char* p = frame->symbol;
    while (*p && (*p == ' ' || (*p >= '0' && *p <= '9'))) p++;
    
    // Get binary name
    char* binary_start = p;
    while (*p && *p != ' ') p++;
    if (*p) {
        *p = '\0';
        frame->file_name = binary_start;
        p++;
    }
    
    // Skip whitespace and address
    while (*p == ' ') p++;
    while (*p && *p != ' ') p++;  // Skip address
    while (*p == ' ') p++;
    
    // Now we should be at function name
    if (*p) {
        char* func_start = p;
        char* plus_sign = strstr(p, " + ");
        
        if (plus_sign) {
            *plus_sign = '\0';
            frame->function_name = func_start;
            char* offset_str = plus_sign + 3;
            frame->offset = (*offset_str != '\0') ? atoi(offset_str) : -1;
        } else {
            frame->function_name = func_start;
        }
    }
#else
    // Unknown platform, just keep the raw string
    frame->function_name = frame->symbol;
#endif
    
    // If we couldn't parse a function name, show the address
    if (!frame->function_name || frame->function_name[0] == '\0') {
        // Reset file_name to prevent dangling pointer into overwritten buffer
        frame->file_name = NULL;
        // Format address as fallback
        snprintf(frame->symbol, MEMROGUE_MAX_SYMBOL_LEN, "0x%lx", 
                 (unsigned long)(uintptr_t)frame->address);
        frame->function_name = frame->symbol;
    }
}

int symbol_resolve_frame(void* address, resolved_frame_t* out_frame) {
    if (!address || !out_frame) {
        return 0;
    }
    
    memset(out_frame, 0, sizeof(*out_frame));
    out_frame->address = address;
    out_frame->offset = -1;
    
#if HAVE_BACKTRACE
    char** symbols = backtrace_symbols(&address, 1);
    
    if (symbols && symbols[0]) {
        parse_symbol_string(symbols[0], out_frame);
        free(symbols);
        return 1;
    }
    
    if (symbols) {
        free(symbols);
    }
#endif
    
    // Fallback: just show the address
    snprintf(out_frame->symbol, MEMROGUE_MAX_SYMBOL_LEN, "0x%lx", 
             (unsigned long)(uintptr_t)address);
    out_frame->function_name = out_frame->symbol;
    
    return 1;
}

resolved_backtrace_t* symbol_resolve(const allocation_info_t* info) {
    if (!info || info->frame_count <= 0) {
        return NULL;
    }
    
    resolved_backtrace_t* bt = (resolved_backtrace_t*)malloc(sizeof(resolved_backtrace_t));
    if (!bt) {
        return NULL;
    }
    
    bt->frame_count = info->frame_count;
    bt->frames = (resolved_frame_t*)calloc((size_t)info->frame_count, sizeof(resolved_frame_t));
    
    if (!bt->frames) {
        free(bt);
        return NULL;
    }
    
#if HAVE_BACKTRACE
    // Resolve all frames at once for efficiency
    char** symbols = backtrace_symbols(info->frames, info->frame_count);
    
    for (int i = 0; i < info->frame_count; i++) {
        bt->frames[i].address = info->frames[i];
        bt->frames[i].offset = -1;
        
        if (symbols && symbols[i]) {
            parse_symbol_string(symbols[i], &bt->frames[i]);
        } else {
            // Fallback
            snprintf(bt->frames[i].symbol, MEMROGUE_MAX_SYMBOL_LEN, "0x%lx",
                     (unsigned long)(uintptr_t)info->frames[i]);
            bt->frames[i].function_name = bt->frames[i].symbol;
        }
    }
    
    if (symbols) {
        free(symbols);
    }
#else
    // No backtrace support, just show addresses
    for (int i = 0; i < info->frame_count; i++) {
        bt->frames[i].address = info->frames[i];
        bt->frames[i].offset = -1;
        snprintf(bt->frames[i].symbol, MEMROGUE_MAX_SYMBOL_LEN, "0x%lx",
                 (unsigned long)(uintptr_t)info->frames[i]);
        bt->frames[i].function_name = bt->frames[i].symbol;
    }
#endif
    
    return bt;
}

void resolved_backtrace_destroy(resolved_backtrace_t* bt) {
    if (!bt) return;
    
    if (bt->frames) {
        free(bt->frames);
    }
    free(bt);
}

int resolved_frame_format(const resolved_frame_t* frame, char* buffer, size_t buffer_size) {
    if (!frame || !buffer || buffer_size == 0) {
        return 0;
    }
    
    int written;
    
    if (frame->function_name && frame->file_name) {
        if (frame->offset >= 0) {
            written = snprintf(buffer, buffer_size, "%s(%s+0x%x) [%p]",
                              frame->file_name, frame->function_name, 
                              (unsigned int)frame->offset, frame->address);
        } else {
            written = snprintf(buffer, buffer_size, "%s(%s) [%p]",
                              frame->file_name, frame->function_name, frame->address);
        }
    } else if (frame->function_name) {
        if (frame->offset >= 0) {
            written = snprintf(buffer, buffer_size, "%s+0x%x [%p]",
                              frame->function_name, (unsigned int)frame->offset, frame->address);
        } else {
            written = snprintf(buffer, buffer_size, "%s [%p]",
                              frame->function_name, frame->address);
        }
    } else {
        written = snprintf(buffer, buffer_size, "[%p]", frame->address);
    }
    
    // Return the number of characters actually written (excluding null terminator)
    // snprintf returns the number that would have been written if buffer was large enough
    return (written < (int)buffer_size) ? written : (int)buffer_size - 1;
}
