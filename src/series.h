#ifndef TSEDGE_SERIES_H
#define TSEDGE_SERIES_H

#include "block.h"
#include "tsedge.h"

#include <stdbool.h>
#include <stddef.h>

#define TSEDGE_MAX_SERIES_NAME 255u

struct tsedge_db;

typedef struct tsedge_series {
    char* name;
    char* dir_path;
    char* segment_path;

    /*
     * Recent appends are accumulated in memory so small writes can be grouped
     * into fixed-size compressed blocks. WAL keeps these buffered points
     * recoverable until they are flushed to a segment.
     */
    tsedge_point* buffer;
    size_t buffer_count;
    size_t buffer_capacity;
} tsedge_series;

int tsedge_series_validate_name(const char* name);
int tsedge_series_init(tsedge_series* series, const char* series_dir, const char* name, bool create_dir);
void tsedge_series_free(tsedge_series* series);
int tsedge_series_append(struct tsedge_db* db, tsedge_series* series, int64_t timestamp, double value);
int tsedge_series_add_recovered_point(struct tsedge_db* db, tsedge_series* series, const tsedge_point* point);

/* Converts the current buffer into one compressed block in the segment file. */
int tsedge_series_flush(struct tsedge_db* db, tsedge_series* series, bool update_wal);

/* Reads persisted blocks plus the not-yet-flushed memory buffer. */
int tsedge_series_read_range(tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_point_callback callback, void* user_data);

/* Computes aggregates while scanning points, without materializing a range. */
int tsedge_series_aggregate(tsedge_series* series, int64_t from_timestamp, int64_t to_timestamp, tsedge_agg_type type, double* out_result);

#endif
