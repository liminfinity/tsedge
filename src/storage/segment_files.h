#ifndef TSEDGE_SEGMENT_FILES_H
#define TSEDGE_SEGMENT_FILES_H

#include "tsedge.h"

#include <stddef.h>
#include <stdint.h>

/* Formats a segment id as the canonical segment_XXXXXX.tse filename. */
int tsedge_segment_format_filename(uint32_t segment_id, char* out, size_t out_size);

/* Parses a canonical segment filename and extracts its numeric id. */
int tsedge_segment_parse_filename(const char* name, uint32_t* out_id);

/* Allocates the full path to a segment file inside a series directory. */
char* tsedge_segment_make_path(const char* series_dir_path, uint32_t segment_id);

/* Returns the size of an existing segment file, or zero when it is unavailable. */
uint64_t tsedge_segment_file_size_or_zero(const char* path);

/* Lists known segment ids for one series directory in sorted order. */
int tsedge_segment_discover_ids(const char* series_dir_path, uint32_t** out_ids, size_t* out_count);

#endif
