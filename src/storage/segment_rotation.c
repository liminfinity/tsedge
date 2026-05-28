#include "segment_rotation.h"
#include "segment_files.h"
#include "series.h"

#include <stdlib.h>

int tsedge_segment_rotate_if_needed(
    const char* series_dir_path,
    uint64_t block_size,
    uint32_t* active_segment_id,
    char** active_segment_path
) {
    if (!series_dir_path || !active_segment_id || !active_segment_path || !*active_segment_path) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /*
     * Rotation is checked before appending a complete block. TSEdge never
     * splits one compressed block across files, which keeps recovery and index
     * rebuild simple.
     */
    uint64_t active_size = tsedge_segment_file_size_or_zero(*active_segment_path);
    if (active_size == 0) {
        return TSEDGE_OK;
    }

    /*
     * TSEDGE_SEGMENT_MAX_BYTES is a build-time knob. Tests and demos can lower
     * it, while normal builds use the default from series.h.
     */
    if (active_size + block_size <= (uint64_t)TSEDGE_SEGMENT_MAX_BYTES) {
        return TSEDGE_OK;
    }

    uint32_t next_segment_id = *active_segment_id + 1u;
    if (next_segment_id == 0) {
        return TSEDGE_ERR_INTERNAL;
    }
    char* next_path = tsedge_segment_make_path(series_dir_path, next_segment_id);
    if (!next_path) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    /*
     * The next segment file is not created here. It appears only when the block
     * writer appends real data, so empty placeholder segment files are avoided.
     */
    free(*active_segment_path);
    *active_segment_path = next_path;
    *active_segment_id = next_segment_id;
    return TSEDGE_OK;
}
