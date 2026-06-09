#include "series_index.h"
#include "segment.h"
#include "segment_files.h"

#include <stdlib.h>

static int segment_id_known(const tsedge_series* series, uint32_t segment_id) {
    for (size_t i = 0; i < series->segment_count; ++i) {
        if (series->segment_ids[i] == segment_id) {
            return 1;
        }
    }
    return 0;
}

static int ensure_segment_id_capacity(tsedge_series* series) {
    if (series->segment_count < series->segment_capacity) {
        return TSEDGE_OK;
    }
    size_t next = series->segment_capacity == 0 ? 4u : series->segment_capacity * 2u;
    uint32_t* resized = (uint32_t*)realloc(series->segment_ids, next * sizeof(*series->segment_ids));
    if (!resized) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    series->segment_ids = resized;
    series->segment_capacity = next;
    return TSEDGE_OK;
}

int tsedge_series_add_segment_id(tsedge_series* series, uint32_t segment_id) {
    if (!series || segment_id == 0) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (segment_id_known(series, segment_id)) {
        return TSEDGE_OK;
    }
    int rc = ensure_segment_id_capacity(series);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    series->segment_ids[series->segment_count++] = segment_id;
    return TSEDGE_OK;
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

int tsedge_series_add_block_index_entry(tsedge_series* series, const tsedge_block_index_entry* entry) {
    if (!series || !entry) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = ensure_block_index_capacity(series);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    series->block_index[series->block_index_count++] = *entry;
    return TSEDGE_OK;
}

static int append_block_index_entries(tsedge_series* series, const tsedge_block_index_entry* entries, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        int rc = tsedge_series_add_block_index_entry(series, &entries[i]);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return TSEDGE_OK;
}

void tsedge_series_clear_index(tsedge_series* series) {
    if (!series) {
        return;
    }
    free(series->segment_ids);
    series->segment_ids = NULL;
    series->segment_count = 0;
    series->segment_capacity = 0;

    free(series->block_index);
    series->block_index = NULL;
    series->block_index_count = 0;
    series->block_index_capacity = 0;
}

int tsedge_series_rebuild_index(tsedge_series* series) {
    if (!series) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    tsedge_series_clear_index(series);

    /*
     * The index is volatile by design. On open or after retention, headers from
     * all segment files are scanned once so later queries can use segment_id and
     * offset instead of rescanning filenames and payloads.
     */
    int rc = tsedge_segment_discover_ids(series->dir_path, &series->segment_ids, &series->segment_count);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    series->segment_capacity = series->segment_count;
    series->active_segment_id = series->segment_count > 0 ? series->segment_ids[series->segment_count - 1u] : 1u;

    for (size_t i = 0; i < series->segment_count; ++i) {
        uint32_t segment_id = series->segment_ids[i];
        char* path = tsedge_segment_make_path(series->dir_path, segment_id);
        if (!path) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        tsedge_block_index_entry* entries = NULL;
        size_t count = 0;
        rc = tsedge_segment_scan_index(path, segment_id, &entries, &count);
        free(path);
        if (rc == TSEDGE_OK) {
            rc = append_block_index_entries(series, entries, count);
        }
        free(entries);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    series->block_index_capacity = series->block_index_count;

    char* next_active_path = tsedge_segment_make_path(series->dir_path, series->active_segment_id);
    if (!next_active_path) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    free(series->active_segment_path);
    series->active_segment_path = next_active_path;
    return TSEDGE_OK;
}
