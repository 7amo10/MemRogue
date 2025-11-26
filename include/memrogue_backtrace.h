#ifndef MEMROGUE_BACKTRACE_H
#define MEMROGUE_BACKTRACE_H

#include "memrogue_allocation_record.h"

// Maximum length for a resolved symbol string
#define MEMROGUE_MAX_SYMBOL_LEN 256

// Maximum number of filter patterns
#define MEMROGUE_MAX_FILTER_PATTERNS 16

// Default number of internal frames to skip (memrogue functions)
#define MEMROGUE_DEFAULT_SKIP_FRAMES 2

/**
 * Structure to hold frame filter configuration.
 * Used to skip internal debugger frames from backtraces.
 */
typedef struct {
    const char* patterns[MEMROGUE_MAX_FILTER_PATTERNS];  // Function name prefixes to filter
    int pattern_count;                                     // Number of active patterns
    int skip_count;                                        // Fixed number of frames to skip
} frame_filter_t;

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

// ============================================================================
// Frame Filtering API
// ============================================================================

/**
 * Initialize a frame filter with default settings.
 * 
 * Sets up the filter with default patterns to skip internal debugger frames:
 * - "memrogue_" prefix (debugger functions)
 * - "backtrace" prefix (backtrace capture functions)
 * 
 * @param filter The filter structure to initialize
 */
void frame_filter_init(frame_filter_t* filter);

/**
 * Initialize a frame filter with custom skip count only (no pattern matching).
 * 
 * @param filter The filter structure to initialize
 * @param skip_count Number of frames to always skip from top of stack
 */
void frame_filter_init_simple(frame_filter_t* filter, int skip_count);

/**
 * Add a function name prefix pattern to the filter.
 * 
 * Frames whose function names start with this prefix will be filtered out.
 * 
 * @param filter The filter to modify
 * @param pattern The function name prefix to filter (e.g., "malloc")
 * @return 1 on success, 0 if filter is full
 */
int frame_filter_add_pattern(frame_filter_t* filter, const char* pattern);

/**
 * Clear all patterns from the filter.
 * 
 * @param filter The filter to clear
 */
void frame_filter_clear(frame_filter_t* filter);

/**
 * Check if a function name should be filtered out.
 * 
 * @param filter The filter to use
 * @param function_name The function name to check
 * @return 1 if the frame should be filtered (skipped), 0 if it should be kept
 */
int frame_filter_should_skip(const frame_filter_t* filter, const char* function_name);

/**
 * Capture backtrace with frame filtering applied.
 * 
 * This enhanced version of backtrace_capture applies pattern-based filtering
 * to remove internal debugger frames, so user code starts from the actual
 * allocation site.
 * 
 * @param info The allocation record to store frames in
 * @param filter The frame filter configuration (may be NULL for no filtering)
 * @return Number of frames captured after filtering, or 0 on failure
 */
int backtrace_capture_filtered(allocation_info_t* info, const frame_filter_t* filter);

/**
 * Get the global default frame filter.
 * 
 * Returns a pointer to a static filter with default patterns configured.
 * This is useful for consistent filtering across the debugger.
 * 
 * @return Pointer to the global default filter (never NULL)
 */
const frame_filter_t* frame_filter_get_default(void);

/**
 * Set the global default frame filter skip count.
 * 
 * @param skip_count Number of frames to skip by default
 */
void frame_filter_set_default_skip(int skip_count);

#endif // MEMROGUE_BACKTRACE_H
