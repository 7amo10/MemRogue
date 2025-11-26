#ifndef MEMROGUE_BACKTRACE_H
#define MEMROGUE_BACKTRACE_H

#include "memrogue_allocation_record.h"

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

#endif // MEMROGUE_BACKTRACE_H
