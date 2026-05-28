#ifndef TSEDGE_SEGMENT_ROTATION_H
#define TSEDGE_SEGMENT_ROTATION_H

#include "tsedge.h"

#include <stdint.h>

int tsedge_segment_rotate_if_needed(
    const char* series_dir_path,
    uint64_t block_size,
    uint32_t* active_segment_id,
    char** active_segment_path
);

#endif
