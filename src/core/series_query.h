#ifndef TSEDGE_SERIES_QUERY_H
#define TSEDGE_SERIES_QUERY_H

#include "series.h"

struct tsedge_db;

/* Streams points from a series over an inclusive timestamp range. */
int tsedge_series_read_range(
    struct tsedge_db* db,
    tsedge_series* series,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data
);
/* Computes one aggregate by scanning matching blocks and buffered points. */
int tsedge_series_aggregate(
    struct tsedge_db* db,
    tsedge_series* series,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_agg_type type,
    double* out_result
);

/* Computes MIN and MAX in one scan for internal benchmarks and diagnostics. */
int tsedge_series_aggregate_min_max(
    struct tsedge_db* db,
    tsedge_series* series,
    int64_t from_timestamp,
    int64_t to_timestamp,
    double* out_min,
    double* out_max
);

/* Computes non-empty half-open window aggregates for one series. */
int tsedge_series_aggregate_windowed(
    struct tsedge_db* db,
    tsedge_series* series,
    int64_t start_time,
    int64_t end_time,
    int64_t window_size,
    tsedge_window_aggregate** out_windows,
    size_t* out_count
);

#endif
