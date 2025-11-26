#ifndef MEMROGUE_ALLOCATION_RECORD_H
#define MEMROGUE_ALLOCATION_RECORD_H

#include <stddef.h>
#include <stdint.h>

// Maximum number of stack frames to capture per allocation
#define MEMROGUE_MAX_FRAMES 16

// Structure to hold allocation information
typedef struct {
    void* ptr;              // Address of the allocation (Key)
    size_t size;            // Size of the allocation
    char* file;             // Source file name (owned copy)
    int line;               // Line number
    uint64_t timestamp;     // Allocation timestamp (or sequence number)
    
    // Stack trace information
    void* frames[MEMROGUE_MAX_FRAMES];  // Array of frame addresses
    int frame_count;                     // Number of valid frames captured
} allocation_info_t;

// Creates a new allocation record
// Copies the filename string to ensure ownership
allocation_info_t* allocation_info_create(void* ptr, size_t size, const char* file, int line, uint64_t timestamp);

// Destroys an allocation record and frees its resources
void allocation_info_destroy(allocation_info_t* info);

#endif // MEMROGUE_ALLOCATION_RECORD_H
