#include "series_query.h"
#include "segment.h"
#include "segment_files.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

int tsedge_series_read_range(tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_point_callback callback, void* user_data) {
    if (!series || !callback || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /*
     * Persisted blocks are scanned through the global in-memory index. Each
     * entry carries its segment id, so ranges can cross segment file boundaries.
     */
    for (size_t i = 0; i < series->block_index_count; ++i) {
        const tsedge_block_index_entry* entry = &series->block_index[i];
        if (entry->max_timestamp < from_timestamp || entry->min_timestamp > to_timestamp) {
            continue;
        }
        char* path = tsedge_segment_make_path(series->dir_path, entry->segment_id);
        if (!path) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        bool stopped = false;
        int rc = tsedge_segment_read_range_indexed(
            path,
            entry,
            1,
            from_timestamp,
            to_timestamp,
            callback,
            user_data,
            &stopped
        );
        free(path);
        if (rc != TSEDGE_OK) {
            return rc;
        }
        if (stopped) {
            return TSEDGE_OK;
        }
    }

    /* The memory buffer is part of the visible series even before it is flushed. */
    for (size_t i = 0; i < series->buffer_count; ++i) {
        if (series->buffer[i].timestamp >= from_timestamp && series->buffer[i].timestamp <= to_timestamp) {
            if (callback(&series->buffer[i], user_data) != 0) {
                return TSEDGE_OK;
            }
        }
    }
    return TSEDGE_OK;
}

static void aggregate_add_value(tsedge_aggregate_state* state, double value) {
    if (state->count == 0) {
        state->min_value = value;
        state->max_value = value;
    }
    if (value < state->min_value) {
        state->min_value = value;
    }
    if (value > state->max_value) {
        state->max_value = value;
    }
    state->sum_value += value;
    ++state->count;
}

int tsedge_series_aggregate(tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_agg_type type, double* out_result) {
    if (!series || !out_result || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /*
     * Aggregation is still streaming for partial blocks and buffers, but fully
     * covered persisted blocks can contribute their stored statistics without
     * decoding every point.
     */
    tsedge_aggregate_state state;
    memset(&state, 0, sizeof(state));
    for (size_t i = 0; i < series->block_index_count; ++i) {
        const tsedge_block_index_entry* entry = &series->block_index[i];
        if (entry->max_timestamp < from_timestamp || entry->min_timestamp > to_timestamp) {
            continue;
        }
        char* path = tsedge_segment_make_path(series->dir_path, entry->segment_id);
        if (!path) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        int rc = tsedge_segment_aggregate(
            path,
            entry,
            1,
            from_timestamp,
            to_timestamp,
            &state
        );
        free(path);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }

    for (size_t i = 0; i < series->buffer_count; ++i) {
        if (series->buffer[i].timestamp >= from_timestamp && series->buffer[i].timestamp <= to_timestamp) {
            aggregate_add_value(&state, series->buffer[i].value);
        }
    }

    if (state.count == 0 && type != TSEDGE_AGG_COUNT) {
        return TSEDGE_ERR_NOT_FOUND;
    }

    switch (type) {
        case TSEDGE_AGG_MIN:
            *out_result = state.min_value;
            return TSEDGE_OK;
        case TSEDGE_AGG_MAX:
            *out_result = state.max_value;
            return TSEDGE_OK;
        case TSEDGE_AGG_SUM:
            *out_result = state.sum_value;
            return TSEDGE_OK;
        case TSEDGE_AGG_AVG:
            *out_result = state.sum_value / (double)state.count;
            return TSEDGE_OK;
        case TSEDGE_AGG_COUNT:
            *out_result = (double)state.count;
            return TSEDGE_OK;
        default:
            return TSEDGE_ERR_INVALID_ARGUMENT;
    }
}
