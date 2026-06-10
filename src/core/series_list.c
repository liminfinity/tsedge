#include "series_list.h"
#include "series_stats.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int stats_to_info(const tsedge_series* series, tsedge_series_info* info) {
    tsedge_series_stats stats;
    int rc = tsedge_series_get_stats(series, &stats);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (stats.segment_count > UINT32_MAX || stats.block_count > UINT32_MAX) {
        return TSEDGE_ERR_INTERNAL;
    }

    memset(info, 0, sizeof(*info));
    snprintf(info->name, sizeof(info->name), "%s", series->name);
    info->total_points = (uint64_t)(stats.total_indexed_points + stats.buffered_points);
    info->segment_count = (uint32_t)stats.segment_count;
    info->block_count = (uint32_t)stats.block_count;
    info->compressed_size_bytes = stats.compressed_size_bytes;
    return TSEDGE_OK;
}

int tsedge_db_list_series(tsedge_db* db, tsedge_series_info** out_series, size_t* out_count) {
    if (!db || !out_series || !out_count) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    *out_series = NULL;
    *out_count = 0;
    if (db->series_count == 0) {
        return TSEDGE_OK;
    }

    tsedge_series_info* list = (tsedge_series_info*)calloc(db->series_count, sizeof(*list));
    if (!list) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    for (size_t i = 0; i < db->series_count; ++i) {
        int rc = stats_to_info(&db->series[i], &list[i]);
        if (rc != TSEDGE_OK) {
            free(list);
            return rc;
        }
    }

    *out_series = list;
    *out_count = db->series_count;
    return TSEDGE_OK;
}
