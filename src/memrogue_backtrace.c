#include "memrogue_backtrace.h"
#include <string.h>

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
