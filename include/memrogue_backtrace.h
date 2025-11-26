#ifndef MEMROGUE_BACKTRACE_H
#define MEMROGUE_BACKTRACE_H

#include "memrogue_allocation_record.h"

// Maximum length for a resolved symbol string
#define MEMROGUE_MAX_SYMBOL_LEN 256

/**
 * Structure to hold resolved symbol information for a single frame.
 */
typedef struct {
    void* address;                          // Original frame address
    char symbol[MEMROGUE_MAX_SYMBOL_LEN];   // Full symbol string (function+offset)
    char* function_name;                    // Pointer into symbol (parsed function name)
    char* file_name;                        // Pointer into symbol (parsed file name, may be NULL)
    int line_number;                        // Line number (0 if unknown)
    int offset;                             // Offset from function start (-1 if unknown)
} resolved_frame_t;

/**
 * Structure to hold all resolved symbols for an allocation.
 */
typedef struct {
    resolved_frame_t* frames;               // Array of resolved frames
    int frame_count;                        // Number of frames
} resolved_backtrace_t;

/**
 * Capture the current call stack into the allocation record.
 * 
 * Uses POSIX backtrace() to capture up to MEMROGUE_MAX_FRAMES frames.
 * The frames are stored directly in the allocation_info_t structure.
 * 
 * @param info The allocation record to store frames in (must not be NULL)
 * @param skip_frames Number of frames to skip from the top of the stack
 *                    (useful for skipping internal debugger frames)
 * @return The number of frames captured, or 0 on failure
 */
int backtrace_capture(allocation_info_t* info, int skip_frames);

/**
 * Check if backtrace functionality is available on this platform.
 * 
 * @return 1 if backtrace is available, 0 otherwise
 */
int backtrace_available(void);

/**
 * Resolve frame addresses to human-readable symbol information.
 * 
 * Uses backtrace_symbols() to convert addresses to function names.
 * Parses the output to extract function name, file, and offset.
 * 
 * @param info The allocation record containing captured frames
 * @return A newly allocated resolved_backtrace_t, or NULL if info is NULL,
 *         has no frames, or allocation fails.
 *         Caller must free with resolved_backtrace_destroy().
 */
resolved_backtrace_t* symbol_resolve(const allocation_info_t* info);

/**
 * Resolve a single frame address to symbol information.
 * 
 * The resolved_frame_t structure is self-contained with all data stored in
 * the inline symbol buffer. It is stack-allocable and does not require cleanup.
 * The function_name and file_name pointers point into the symbol buffer.
 * 
 * @param address The frame address to resolve
 * @param out_frame Output structure to fill with resolved info
 * @return 1 on success, 0 on failure
 */
int symbol_resolve_frame(void* address, resolved_frame_t* out_frame);

/**
 * Free a resolved backtrace structure.
 * 
 * @param bt The resolved backtrace to free (may be NULL)
 */
void resolved_backtrace_destroy(resolved_backtrace_t* bt);

/**
 * Format a resolved frame as a human-readable string.
 * 
 * @param frame The resolved frame to format
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator).
 *         Returns 0 if frame or buffer is NULL, or if buffer_size is 0.
 */
int resolved_frame_format(const resolved_frame_t* frame, char* buffer, size_t buffer_size);

#endif // MEMROGUE_BACKTRACE_H
