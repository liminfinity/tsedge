#define _POSIX_C_SOURCE 200809L

#include "series_retention.h"
#include "series_index.h"
#include "segment_files.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    uint32_t segment_id;
    int64_t min_timestamp;
    int64_t max_timestamp;
    size_t block_count;
} segment_bounds;

static segment_bounds* find_segment_bounds(segment_bounds* bounds, size_t count, uint32_t segment_id) {
    for (size_t i = 0; i < count; ++i) {
        if (bounds[i].segment_id == segment_id) {
            return &bounds[i];
        }
    }
    return NULL;
}

static int add_segment_bounds(segment_bounds** bounds, size_t* count, size_t* capacity, const tsedge_block_index_entry* entry) {
    segment_bounds* existing = find_segment_bounds(*bounds, *count, entry->segment_id);
    if (existing) {
        if (entry->min_timestamp < existing->min_timestamp) {
            existing->min_timestamp = entry->min_timestamp;
        }
        if (entry->max_timestamp > existing->max_timestamp) {
            existing->max_timestamp = entry->max_timestamp;
        }
        ++existing->block_count;
        return TSEDGE_OK;
    }

    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 8u : *capacity * 2u;
        segment_bounds* resized = (segment_bounds*)realloc(*bounds, next * sizeof(**bounds));
        if (!resized) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        *bounds = resized;
        *capacity = next;
    }

    (*bounds)[*count].segment_id = entry->segment_id;
    (*bounds)[*count].min_timestamp = entry->min_timestamp;
    (*bounds)[*count].max_timestamp = entry->max_timestamp;
    (*bounds)[*count].block_count = 1u;
    ++(*count);
    return TSEDGE_OK;
}

int tsedge_series_delete_before(tsedge_db* db, tsedge_series* series, int64_t older_than_timestamp) {
    if (!db || !series) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /*
     * Retention works at segment granularity. Flushing first moves visible
     * buffered data out of WAL and onto disk so deletion decisions use segment
     * metadata only.
     */
    int rc = tsedge_series_flush(db, series, true);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (series->block_index_count == 0) {
        return TSEDGE_OK;
    }

    segment_bounds* bounds = NULL;
    size_t bounds_count = 0;
    size_t bounds_capacity = 0;
    for (size_t i = 0; i < series->block_index_count; ++i) {
        rc = add_segment_bounds(&bounds, &bounds_count, &bounds_capacity, &series->block_index[i]);
        if (rc != TSEDGE_OK) {
            free(bounds);
            return rc;
        }
    }

    int delete_rc = TSEDGE_OK;
    bool deleted_any = false;
    for (size_t i = 0; i < bounds_count; ++i) {
        if (bounds[i].max_timestamp >= older_than_timestamp) {
            continue;
        }

        /*
         * Retention removes only whole segment files. A partially old segment is
         * kept because rewriting it would be compaction, which this prototype
         * intentionally does not implement.
         */
        char* path = tsedge_segment_make_path(series->dir_path, bounds[i].segment_id);
        if (!path) {
            delete_rc = TSEDGE_ERR_NO_MEMORY;
            break;
        }
        if (unlink(path) != 0 && errno != ENOENT) {
            delete_rc = TSEDGE_ERR_IO;
            free(path);
            break;
        }
        free(path);
        deleted_any = true;
    }
    free(bounds);

    if (deleted_any || delete_rc != TSEDGE_OK) {
        /*
         * Remaining segment files keep their original ids. Rebuilding the index
         * from disk preserves gaps such as segment_000007.tse after older files
         * were removed.
         */
        int rebuild_rc = tsedge_series_rebuild_index(series);
        if (delete_rc != TSEDGE_OK) {
            return delete_rc;
        }
        if (rebuild_rc != TSEDGE_OK) {
            return rebuild_rc;
        }
    }
    return TSEDGE_OK;
}
