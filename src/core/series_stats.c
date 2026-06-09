#include "series_stats.h"
#include "segment_files.h"

#include <string.h>
#include <stdlib.h>

static void stats_add_timestamp(tsedge_series_stats* stats, int64_t timestamp) {
    if (!stats->has_time_range) {
        stats->has_time_range = 1;
        stats->min_timestamp = timestamp;
        stats->max_timestamp = timestamp;
        return;
    }
    if (timestamp < stats->min_timestamp) {
        stats->min_timestamp = timestamp;
    }
    if (timestamp > stats->max_timestamp) {
        stats->max_timestamp = timestamp;
    }
}

int tsedge_series_get_stats(const tsedge_series* series, tsedge_series_stats* out_stats) {
    if (!series || !out_stats) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->block_count = series->block_index_count;
    out_stats->buffered_points = series->buffer_count;
    out_stats->segment_count = series->segment_count;
    out_stats->active_segment_id = series->active_segment_id;

    /*
     * Statistics are derived from block metadata and the live buffer only. This
     * gives callers a cheap series summary without decompressing segment data.
     */
    for (size_t i = 0; i < series->block_index_count; ++i) {
        const tsedge_block_index_entry* entry = &series->block_index[i];
        out_stats->total_indexed_points += entry->point_count;
        stats_add_timestamp(out_stats, entry->min_timestamp);
        stats_add_timestamp(out_stats, entry->max_timestamp);
    }

    for (size_t i = 0; i < series->buffer_count; ++i) {
        stats_add_timestamp(out_stats, series->buffer[i].timestamp);
    }

    for (size_t i = 0; i < series->segment_count; ++i) {
        char* path = tsedge_segment_make_path(series->dir_path, series->segment_ids[i]);
        if (!path) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        uint64_t size = tsedge_segment_file_size_or_zero(path);
        free(path);
        out_stats->total_segment_size_bytes += size;
    }
    out_stats->segment_size_bytes = out_stats->total_segment_size_bytes;
    return TSEDGE_OK;
}
