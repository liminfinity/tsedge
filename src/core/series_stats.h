#ifndef TSEDGE_SERIES_STATS_H
#define TSEDGE_SERIES_STATS_H

#include "series.h"

/* Collects lightweight series statistics from buffers, index and segment sizes. */
int tsedge_series_get_stats(const tsedge_series* series, tsedge_series_stats* out_stats);

#endif
