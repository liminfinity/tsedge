#ifndef TSEDGE_SERIES_QUERY_H
#define TSEDGE_SERIES_QUERY_H

#include "series.h"

/* Streams points from a series over an inclusive timestamp range. */
int tsedge_series_read_range(
    tsedge_series* series,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data
);
/* Computes one aggregate by scanning matching blocks and buffered points. */
int tsedge_series_aggregate(
    tsedge_series* series,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_agg_type type,
    double* out_result
);

#endif
