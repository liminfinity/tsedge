#ifndef TSEDGE_SEGMENT_H
#define TSEDGE_SEGMENT_H

#include "block.h"
#include "tsedge.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    double min_value;
    double max_value;
    double sum_value;
    size_t count;
} tsedge_aggregate_state;

/*
 * A segment is an append-only file containing a sequence of compressed blocks.
 * The segment layer owns file traversal and delegates block metadata and
 * compression details to block.c and compress.c.
 */
int tsedge_segment_append_block(
    const char* segment_path,
    uint32_t segment_id,
    const tsedge_point* points,
    size_t count,
    tsedge_block_index_entry* out_index_entry
);

/* Estimates how many bytes a compressed block would add to a segment. */
int tsedge_segment_estimate_block_size(const tsedge_point* points, size_t count, uint64_t* out_size);

/* Builds block index entries by scanning a segment file. */
int tsedge_segment_scan_index(const char* segment_path, uint32_t segment_id, tsedge_block_index_entry** out_entries, size_t* out_count);

/* Reads a range by scanning blocks sequentially in one segment. */
int tsedge_segment_read_range(
    const char* segment_path,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data,
    bool* out_stopped
);

/* Reads a range using prefiltered block index entries. */
int tsedge_segment_read_range_indexed(
    const char* segment_path,
    const tsedge_block_index_entry* entries,
    size_t entry_count,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data,
    bool* out_stopped
);

/* Updates aggregate state from indexed blocks in one segment. */
int tsedge_segment_aggregate(
    const char* segment_path,
    const tsedge_block_index_entry* entries,
    size_t entry_count,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_aggregate_state* state
);

#endif
