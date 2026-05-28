#ifndef TSEDGE_SEGMENT_FILES_H
#define TSEDGE_SEGMENT_FILES_H

#include "tsedge.h"

#include <stddef.h>
#include <stdint.h>

int tsedge_segment_format_filename(uint32_t segment_id, char* out, size_t out_size);
int tsedge_segment_parse_filename(const char* name, uint32_t* out_id);
char* tsedge_segment_make_path(const char* series_dir_path, uint32_t segment_id);
uint64_t tsedge_segment_file_size_or_zero(const char* path);
int tsedge_segment_discover_ids(const char* series_dir_path, uint32_t** out_ids, size_t* out_count);

#endif
