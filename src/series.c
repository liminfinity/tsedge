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
    int rc = tsedge_segment_append_block(series->segment_path, series->buffer, series->buffer_count);
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
    int rc = tsedge_segment_read_range(series->segment_path, from_timestamp, to_timestamp, callback, user_data);
    if (rc != TSEDGE_OK) {
        return rc;
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

typedef struct {
    double min_value;
    double max_value;
    double sum;
    size_t count;
} aggregate_ctx;

static int aggregate_cb(const tsedge_point* point, void* user_data) {
    aggregate_ctx* ctx = (aggregate_ctx*)user_data;
    if (ctx->count == 0) {
        ctx->min_value = point->value;
        ctx->max_value = point->value;
    }
    if (point->value < ctx->min_value) {
        ctx->min_value = point->value;
    }
    if (point->value > ctx->max_value) {
        ctx->max_value = point->value;
    }
    ctx->sum += point->value;
    ++ctx->count;
    return 0;
}

int tsedge_series_aggregate(tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_agg_type type, double* out_result) {
    if (!out_result) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /*
     * Aggregation reuses read_range callbacks and updates a small accumulator,
     * so large query ranges do not require allocating all decoded points.
     */
    aggregate_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    int rc = tsedge_series_read_range(series, from_timestamp, to_timestamp, aggregate_cb, &ctx);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (ctx.count == 0 && type != TSEDGE_AGG_COUNT) {
        return TSEDGE_ERR_NOT_FOUND;
    }

    switch (type) {
        case TSEDGE_AGG_MIN:
            *out_result = ctx.min_value;
            return TSEDGE_OK;
        case TSEDGE_AGG_MAX:
            *out_result = ctx.max_value;
            return TSEDGE_OK;
        case TSEDGE_AGG_SUM:
            *out_result = ctx.sum;
            return TSEDGE_OK;
        case TSEDGE_AGG_AVG:
            *out_result = ctx.sum / (double)ctx.count;
            return TSEDGE_OK;
        case TSEDGE_AGG_COUNT:
            *out_result = (double)ctx.count;
            return TSEDGE_OK;
        default:
            return TSEDGE_ERR_INVALID_ARGUMENT;
    }
}
