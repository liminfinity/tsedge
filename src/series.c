#define _POSIX_C_SOURCE 200809L

#include "series.h"
#include "db.h"
#include "segment.h"
#include "wal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* xstrdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

int tsedge_series_validate_name(const char* name) {
    if (!name || name[0] == '\0' || strlen(name) > TSEDGE_MAX_SERIES_NAME) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    for (const char* p = name; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            return TSEDGE_ERR_INVALID_ARGUMENT;
        }
    }
    return TSEDGE_OK;
}

static int create_metadata(const tsedge_series* series) {
    char* metadata_path = tsedge_path_join(series->dir_path, "metadata.txt");
    if (!metadata_path) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    FILE* f = fopen(metadata_path, "w");
    free(metadata_path);
    if (!f) {
        return TSEDGE_ERR_IO;
    }
    fprintf(f, "name=%s\nblock_max_points=%u\nsegment=segment_000001.tse\n", series->name, TSEDGE_BLOCK_MAX_POINTS);
    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

static int ensure_block_index_capacity(tsedge_series* series) {
    if (series->block_index_count < series->block_index_capacity) {
        return TSEDGE_OK;
    }
    size_t next = series->block_index_capacity == 0 ? 16u : series->block_index_capacity * 2u;
    tsedge_block_index_entry* resized = (tsedge_block_index_entry*)realloc(series->block_index, next * sizeof(*series->block_index));
    if (!resized) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    series->block_index = resized;
    series->block_index_capacity = next;
    return TSEDGE_OK;
}

static int add_block_index_entry(tsedge_series* series, const tsedge_block_index_entry* entry) {
    int rc = ensure_block_index_capacity(series);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    series->block_index[series->block_index_count++] = *entry;
    return TSEDGE_OK;
}

int tsedge_series_init(tsedge_series* series, const char* series_dir, const char* name, bool create_dir) {
    if (!series || !series_dir || !name) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    memset(series, 0, sizeof(*series));
    series->name = xstrdup(name);
    series->dir_path = tsedge_path_join(series_dir, name);
    if (!series->name || !series->dir_path) {
        tsedge_series_free(series);
        return TSEDGE_ERR_NO_MEMORY;
    }
    series->segment_path = tsedge_path_join(series->dir_path, "segment_000001.tse");
    if (!series->segment_path) {
        tsedge_series_free(series);
        return TSEDGE_ERR_NO_MEMORY;
    }
    series->buffer_capacity = TSEDGE_BLOCK_MAX_POINTS;
    series->buffer = (tsedge_point*)malloc(series->buffer_capacity * sizeof(*series->buffer));
    if (!series->buffer) {
        tsedge_series_free(series);
        return TSEDGE_ERR_NO_MEMORY;
    }

    if (create_dir) {
        int rc = tsedge_mkdir_if_needed(series->dir_path);
        if (rc == TSEDGE_OK) {
            rc = create_metadata(series);
        }
        if (rc != TSEDGE_OK) {
            tsedge_series_free(series);
            return rc;
        }
    }

    /*
     * The index is intentionally in-memory only. Opening a series scans block
     * headers once, then range reads can seek directly to relevant offsets.
     */
    int rc = tsedge_segment_scan_index(series->segment_path, &series->block_index, &series->block_index_count);
    if (rc != TSEDGE_OK) {
        tsedge_series_free(series);
        return rc;
    }
    series->block_index_capacity = series->block_index_count;
    return TSEDGE_OK;
}

void tsedge_series_free(tsedge_series* series) {
    if (!series) {
        return;
    }
    free(series->name);
    free(series->dir_path);
    free(series->segment_path);
    free(series->buffer);
    free(series->block_index);
    memset(series, 0, sizeof(*series));
}

int tsedge_series_flush(tsedge_db* db, tsedge_series* series, bool update_wal) {
    if (!series) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (series->buffer_count == 0) {
        return TSEDGE_OK;
    }

    /*
     * A flush is the point where buffered rows become an immutable compressed
     * block on disk. After that, WAL can forget the flushed rows.
     */
    tsedge_block_index_entry entry;
    int rc = tsedge_segment_append_block(series->segment_path, series->buffer, series->buffer_count, &entry);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = add_block_index_entry(series, &entry);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    series->buffer_count = 0;
    return update_wal ? tsedge_wal_truncate_to_buffers(db) : TSEDGE_OK;
}

static int add_point_to_buffer(tsedge_db* db, tsedge_series* series, const tsedge_point* point, bool update_wal) {
    /*
     * The fixed-size buffer bounds memory use and gives compression a block of
     * nearby points, which is important for delta-of-delta timestamps.
     */
    if (series->buffer_count >= series->buffer_capacity) {
        int rc = tsedge_series_flush(db, series, update_wal);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    series->buffer[series->buffer_count++] = *point;
    return TSEDGE_OK;
}

int tsedge_series_append(tsedge_db* db, tsedge_series* series, int64_t timestamp, double value) {
    if (!db || !series) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    tsedge_point point;
    point.timestamp = timestamp;
    point.value = value;

    if (series->buffer_count >= series->buffer_capacity) {
        int rc = tsedge_series_flush(db, series, true);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }

    /*
     * WAL is written before changing the in-memory buffer. If the process dies
     * after this point, open/replay can reconstruct the accepted append.
     */
    int rc = tsedge_wal_append(db, series->name, &point);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return add_point_to_buffer(db, series, &point, true);
}

int tsedge_series_add_recovered_point(tsedge_db* db, tsedge_series* series, const tsedge_point* point) {
    if (!db || !series || !point) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    return add_point_to_buffer(db, series, point, false);
}

int tsedge_series_read_range(tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_point_callback callback, void* user_data) {
    if (!series || !callback || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /*
     * Persisted blocks are scanned first. Segment metadata lets the lower layer
     * skip blocks whose min/max timestamps cannot match the requested range.
     */
    bool stopped = false;
    int rc = tsedge_segment_read_range_indexed(
        series->segment_path,
        series->block_index,
        series->block_index_count,
        from_timestamp,
        to_timestamp,
        callback,
        user_data,
        &stopped
    );
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (stopped) {
        return TSEDGE_OK;
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
     * covered persisted blocks can now contribute their stored statistics
     * without decoding every point.
     */
    tsedge_aggregate_state state;
    memset(&state, 0, sizeof(state));
    int rc = tsedge_segment_aggregate(
        series->segment_path,
        series->block_index,
        series->block_index_count,
        from_timestamp,
        to_timestamp,
        &state
    );
    if (rc != TSEDGE_OK) {
        return rc;
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
