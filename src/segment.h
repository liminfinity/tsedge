#ifndef TSEDGE_SEGMENT_H
#define TSEDGE_SEGMENT_H

#include "tsedge.h"

#include <stddef.h>

/*
 * A segment is an append-only file containing a sequence of compressed blocks.
 * The segment layer owns file traversal and delegates block metadata and
 * compression details to block.c and compress.c.
 */
int tsedge_segment_append_block(const char* segment_path, const tsedge_point* points, size_t count);
int tsedge_segment_read_range(const char* segment_path, int64_t from_timestamp, int64_t to_timestamp, tsedge_point_callback callback, void* user_data);

#endif
