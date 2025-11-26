#include "memrogue_allocation_record.h"
#include <stdlib.h>
#include <string.h>

allocation_info_t* allocation_info_create(void* ptr, size_t size, const char* file, int line, uint64_t timestamp) {
    allocation_info_t* info = (allocation_info_t*)malloc(sizeof(allocation_info_t));
    if (!info) return NULL;

    info->ptr = ptr;
    info->size = size;
    info->line = line;
    info->timestamp = timestamp;
    
    // Initialize stack trace fields
    info->frame_count = 0;
    memset(info->frames, 0, sizeof(info->frames));

    if (file) {
        info->file = strdup(file);
        if (!info->file) {
            free(info);
            return NULL;
        }
    } else {
        info->file = NULL;
    }

    return info;
}

void allocation_info_destroy(allocation_info_t* info) {
    if (!info) return;

    if (info->file) {
        free(info->file);
    }
    free(info);
}
