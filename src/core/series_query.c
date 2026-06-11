#include "series_query.h"
#include "block.h"
#include "compress.h"
#include "db.h"
#include "segment.h"
#include "segment_files.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int block_fully_covered_index(const tsedge_block_index_entry* entry, int64_t from_timestamp, int64_t to_timestamp) {
    return entry->min_timestamp >= from_timestamp && entry->max_timestamp <= to_timestamp;
}

typedef struct {
    uint64_t count;
    double min_value;
    double max_value;
    double sum_value;
} window_accumulator;

static int block_intersects_window_range(const tsedge_block_index_entry* entry, int64_t start_time, int64_t end_time) {
    return entry->max_timestamp >= start_time && entry->min_timestamp < end_time;
}

static int validate_window_range(int64_t start_time, int64_t end_time, int64_t window_size, size_t* out_window_count) {
    if (!out_window_count || window_size <= 0 || start_time > end_time) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    *out_window_count = 0;
    if (start_time == end_time) {
        return TSEDGE_OK;
    }
    if (start_time < 0 && end_time > INT64_MAX + start_time) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    int64_t range = end_time - start_time;
    uint64_t window_count = (uint64_t)(range / window_size);
    if ((range % window_size) != 0) {
        ++window_count;
    }
    if (window_count > (uint64_t)TSEDGE_MAX_WINDOW_AGGREGATES || window_count > (uint64_t)SIZE_MAX) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    *out_window_count = (size_t)window_count;
    return TSEDGE_OK;
}

static size_t window_index_for_timestamp(int64_t timestamp, int64_t start_time, int64_t window_size) {
    return (size_t)((timestamp - start_time) / window_size);
}

static void window_add_value(window_accumulator* window, double value) {
    if (window->count == 0) {
        window->min_value = value;
        window->max_value = value;
    }
    if (value < window->min_value) {
        window->min_value = value;
    }
    if (value > window->max_value) {
        window->max_value = value;
    }
    window->sum_value += value;
    ++window->count;
}

static void window_add_block(window_accumulator* window, const tsedge_block_header* header) {
    if (window->count == 0) {
        window->min_value = header->min_value;
        window->max_value = header->max_value;
    }
    if (header->min_value < window->min_value) {
        window->min_value = header->min_value;
    }
    if (header->max_value > window->max_value) {
        window->max_value = header->max_value;
    }
    window->sum_value += header->sum_value;
    window->count += header->point_count;
}

static int decode_block_to_windows(
    FILE* f,
    const tsedge_block_header* header,
    int64_t start_time,
    int64_t end_time,
    int64_t window_size,
    window_accumulator* windows,
    size_t window_count
) {
    uint8_t* timestamp_data = NULL;
    uint8_t* value_data = NULL;
    int64_t* timestamps = (int64_t*)malloc((size_t)header->point_count * sizeof(*timestamps));
    double* values = (double*)malloc((size_t)header->point_count * sizeof(*values));
    if (!timestamps || !values) {
        free(timestamps);
        free(values);
        return TSEDGE_ERR_NO_MEMORY;
    }

    int rc = tsedge_block_read_payload(f, header, &timestamp_data, &value_data);
    if (rc == TSEDGE_OK) {
        rc = tsedge_decompress_timestamps(timestamp_data, header->timestamp_size, header->point_count, timestamps);
    }
    if (rc == TSEDGE_OK) {
        rc = tsedge_decompress_values(value_data, header->value_size, header->point_count, values);
    }
    if (rc == TSEDGE_OK) {
        for (uint32_t i = 0; i < header->point_count; ++i) {
            if (timestamps[i] >= start_time && timestamps[i] < end_time) {
                size_t index = window_index_for_timestamp(timestamps[i], start_time, window_size);
                if (index >= window_count) {
                    free(timestamp_data);
                    free(value_data);
                    free(timestamps);
                    free(values);
                    return TSEDGE_ERR_CORRUPT;
                }
                window_add_value(&windows[index], values[i]);
            }
        }
    }

    free(timestamp_data);
    free(value_data);
    free(timestamps);
    free(values);
    return rc;
}

int tsedge_series_read_range(tsedge_db* db, tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_point_callback callback, void* user_data) {
    if (!db || !series || !callback || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    db->read_debug_stats.blocks_total += series->block_index_count;

    /*
     * Persisted blocks are scanned through the global in-memory index. Each
     * entry carries its segment id, so ranges can cross segment file boundaries.
     */
    for (size_t i = 0; i < series->block_index_count; ++i) {
        const tsedge_block_index_entry* entry = &series->block_index[i];
        ++db->read_debug_stats.blocks_scanned;
        if (entry->max_timestamp < from_timestamp || entry->min_timestamp > to_timestamp) {
            ++db->read_debug_stats.blocks_skipped;
            continue;
        }
        ++db->read_debug_stats.blocks_decoded;
        db->read_debug_stats.points_decoded += entry->point_count;
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

static int aggregate_series_state(tsedge_db* db, tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_aggregate_state* state) {
    if (!db || !series || !state || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /*
     * Aggregation is still streaming for partial blocks and buffers, but fully
     * covered persisted blocks can contribute their stored statistics without
     * decoding every point.
     */
    memset(state, 0, sizeof(*state));
    db->read_debug_stats.blocks_total += series->block_index_count;
    for (size_t i = 0; i < series->block_index_count; ++i) {
        const tsedge_block_index_entry* entry = &series->block_index[i];
        ++db->read_debug_stats.blocks_scanned;
        if (entry->max_timestamp < from_timestamp || entry->min_timestamp > to_timestamp) {
            ++db->read_debug_stats.blocks_skipped;
            continue;
        }
        if (!block_fully_covered_index(entry, from_timestamp, to_timestamp)) {
            ++db->read_debug_stats.blocks_decoded;
            db->read_debug_stats.points_decoded += entry->point_count;
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
            state
        );
        free(path);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }

    for (size_t i = 0; i < series->buffer_count; ++i) {
        if (series->buffer[i].timestamp >= from_timestamp && series->buffer[i].timestamp <= to_timestamp) {
            aggregate_add_value(state, series->buffer[i].value);
        }
    }

    return TSEDGE_OK;
}

int tsedge_series_aggregate(tsedge_db* db, tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_agg_type type, double* out_result) {
    if (!db || !series || !out_result || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    tsedge_aggregate_state state;
    int rc = aggregate_series_state(db, series, from_timestamp, to_timestamp, &state);
    if (rc != TSEDGE_OK) {
        return rc;
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

int tsedge_series_aggregate_min_max(tsedge_db* db, tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, double* out_min, double* out_max) {
    if (!db || !series || !out_min || !out_max || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    tsedge_aggregate_state state;
    int rc = aggregate_series_state(db, series, from_timestamp, to_timestamp, &state);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (state.count == 0) {
        return TSEDGE_ERR_NOT_FOUND;
    }

    *out_min = state.min_value;
    *out_max = state.max_value;
    return TSEDGE_OK;
}

int tsedge_series_aggregate_windowed(
    tsedge_db* db,
    tsedge_series* series,
    int64_t start_time,
    int64_t end_time,
    int64_t window_size,
    tsedge_window_aggregate** out_windows,
    size_t* out_count
) {
    if (!db || !series || !out_windows || !out_count) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    *out_windows = NULL;
    *out_count = 0;

    size_t window_count = 0;
    int rc = validate_window_range(start_time, end_time, window_size, &window_count);
    if (rc != TSEDGE_OK || window_count == 0) {
        return rc;
    }

    window_accumulator* windows = (window_accumulator*)calloc(window_count, sizeof(*windows));
    if (!windows) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    db->read_debug_stats.blocks_total += series->block_index_count;
    for (size_t i = 0; i < series->block_index_count; ++i) {
        const tsedge_block_index_entry* entry = &series->block_index[i];
        ++db->read_debug_stats.blocks_scanned;
        if (!block_intersects_window_range(entry, start_time, end_time)) {
            ++db->read_debug_stats.blocks_skipped;
            continue;
        }

        int can_use_metadata = entry->min_timestamp >= start_time && entry->max_timestamp < end_time;
        size_t block_window_index = 0;
        if (can_use_metadata) {
            size_t min_index = window_index_for_timestamp(entry->min_timestamp, start_time, window_size);
            size_t max_index = window_index_for_timestamp(entry->max_timestamp, start_time, window_size);
            can_use_metadata = min_index == max_index && min_index < window_count;
            block_window_index = min_index;
        }
        if (!can_use_metadata) {
            ++db->read_debug_stats.blocks_decoded;
            db->read_debug_stats.points_decoded += entry->point_count;
        }

        char* path = tsedge_segment_make_path(series->dir_path, entry->segment_id);
        if (!path) {
            free(windows);
            return TSEDGE_ERR_NO_MEMORY;
        }
        FILE* f = fopen(path, "rb");
        free(path);
        if (!f) {
            free(windows);
            return TSEDGE_ERR_IO;
        }
        if (fseek(f, entry->offset, SEEK_SET) != 0) {
            fclose(f);
            free(windows);
            return TSEDGE_ERR_IO;
        }

        tsedge_block_header header;
        bool eof = false;
        rc = tsedge_block_read_header(f, &header, &eof);
        if (rc != TSEDGE_OK || eof) {
            fclose(f);
            free(windows);
            return eof ? TSEDGE_ERR_CORRUPT : rc;
        }

        if (can_use_metadata) {
            window_add_block(&windows[block_window_index], &header);
        } else {
            rc = decode_block_to_windows(f, &header, start_time, end_time, window_size, windows, window_count);
        }
        if (fclose(f) != 0 && rc == TSEDGE_OK) {
            rc = TSEDGE_ERR_IO;
        }
        if (rc != TSEDGE_OK) {
            free(windows);
            return rc;
        }
    }

    for (size_t i = 0; i < series->buffer_count; ++i) {
        int64_t timestamp = series->buffer[i].timestamp;
        if (timestamp >= start_time && timestamp < end_time) {
            size_t index = window_index_for_timestamp(timestamp, start_time, window_size);
            if (index >= window_count) {
                free(windows);
                return TSEDGE_ERR_INTERNAL;
            }
            window_add_value(&windows[index], series->buffer[i].value);
        }
    }

    size_t non_empty = 0;
    for (size_t i = 0; i < window_count; ++i) {
        if (windows[i].count > 0) {
            ++non_empty;
        }
    }
    if (non_empty == 0) {
        free(windows);
        return TSEDGE_OK;
    }

    tsedge_window_aggregate* result = (tsedge_window_aggregate*)calloc(non_empty, sizeof(*result));
    if (!result) {
        free(windows);
        return TSEDGE_ERR_NO_MEMORY;
    }
    size_t out_index = 0;
    for (size_t i = 0; i < window_count; ++i) {
        if (windows[i].count == 0) {
            continue;
        }
        int64_t window_start = start_time + (int64_t)i * window_size;
        int64_t remaining = end_time - window_start;
        int64_t window_end = remaining < window_size ? end_time : window_start + window_size;
        result[out_index].window_start = window_start;
        result[out_index].window_end = window_end;
        result[out_index].count = windows[i].count;
        result[out_index].min = windows[i].min_value;
        result[out_index].max = windows[i].max_value;
        result[out_index].avg = windows[i].sum_value / (double)windows[i].count;
        ++out_index;
    }

    free(windows);
    *out_windows = result;
    *out_count = non_empty;
    return TSEDGE_OK;
}
