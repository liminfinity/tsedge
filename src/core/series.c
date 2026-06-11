#define _POSIX_C_SOURCE 200809L

#include "db_quota.h"
#include "series.h"
#include "db.h"
#include "segment.h"
#include "segment_files.h"
#include "segment_rotation.h"
#include "series_index.h"
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
    fprintf(f, "name=%s\nblock_max_points=%u\nsegment_pattern=segment_%%06u.tse\n", series->name, TSEDGE_BLOCK_MAX_POINTS);
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
    series->active_segment_id = 1u;
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
     * The index is intentionally in-memory only. Opening a series discovers all
     * segment_*.tse files, scans block headers once, and remembers both segment
     * id and block offset for later reads.
     */
    int rc = tsedge_series_rebuild_index(series);
    if (rc != TSEDGE_OK) {
        tsedge_series_free(series);
        return rc;
    }
    return TSEDGE_OK;
}

void tsedge_series_free(tsedge_series* series) {
    if (!series) {
        return;
    }
    free(series->name);
    free(series->dir_path);
    free(series->active_segment_path);
    free(series->segment_ids);
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
    int rc = tsedge_wal_flush(db);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    uint64_t block_size = 0;
    rc = tsedge_segment_estimate_block_size(series->buffer, series->buffer_count, &block_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    rc = tsedge_segment_rotate_if_needed(
        series->dir_path,
        block_size,
        &series->active_segment_id,
        &series->active_segment_path
    );
    if (rc != TSEDGE_OK) {
        return rc;
    }

    tsedge_block_index_entry entry;
    rc = tsedge_segment_append_block(series->active_segment_path, series->active_segment_id, series->buffer, series->buffer_count, &entry);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = tsedge_series_add_segment_id(series, series->active_segment_id);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = tsedge_series_add_block_index_entry(series, &entry);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    series->buffer_count = 0;
    if (!update_wal) {
        return TSEDGE_OK;
    }
    rc = tsedge_wal_truncate_to_buffers(db);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return tsedge_db_enforce_disk_quota(db);
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

static int append_point_internal(tsedge_db* db, tsedge_series* series, const tsedge_point* point, bool write_wal, bool update_wal_on_flush) {
    if (!db || !series || !point) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    if (series->buffer_count >= series->buffer_capacity) {
        int rc = tsedge_series_flush(db, series, update_wal_on_flush);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }

    /*
     * WAL is written before changing the in-memory buffer. If the process dies
     * after this point, open/replay can reconstruct the accepted append.
     */
    if (write_wal) {
        int rc = tsedge_wal_append(db, series->name, point);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return add_point_to_buffer(db, series, point, update_wal_on_flush);
}

int tsedge_series_append(tsedge_db* db, tsedge_series* series, int64_t timestamp, double value) {
    tsedge_point point;
    point.timestamp = timestamp;
    point.value = value;
    return append_point_internal(db, series, &point, true, true);
}

int tsedge_series_append_batch(tsedge_db* db, tsedge_series* series, const tsedge_point* points, size_t count) {
    if (!db || !series || (!points && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    size_t offset = 0;
    while (offset < count) {
        if (series->buffer_count >= series->buffer_capacity) {
            int rc = tsedge_series_flush(db, series, true);
            if (rc != TSEDGE_OK) {
                return rc;
            }
        }

        size_t available = series->buffer_capacity - series->buffer_count;
        size_t chunk = count - offset;
        if (chunk > available) {
            chunk = available;
        }

        /*
         * WAL is written for the whole chunk before any point from that chunk is
         * copied into the memory buffer. If the WAL write fails, the chunk is
         * rejected before visible in-memory state changes.
         */
        int rc = tsedge_wal_append_batch(db, series->name, points + offset, chunk);
        if (rc != TSEDGE_OK) {
            return rc;
        }
        memcpy(series->buffer + series->buffer_count, points + offset, chunk * sizeof(*points));
        series->buffer_count += chunk;
        offset += chunk;
    }
    return TSEDGE_OK;
}

int tsedge_series_add_recovered_point(tsedge_db* db, tsedge_series* series, const tsedge_point* point) {
    if (!db || !series || !point) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    return append_point_internal(db, series, point, false, false);
}
