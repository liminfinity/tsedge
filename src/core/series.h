#ifndef TSEDGE_SERIES_H
#define TSEDGE_SERIES_H

#include "block.h"
#include "tsedge.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TSEDGE_MAX_SERIES_NAME 255u
#ifndef TSEDGE_SEGMENT_MAX_BYTES
#define TSEDGE_SEGMENT_MAX_BYTES (64u * 1024u * 1024u)
#endif

struct tsedge_db;

typedef struct tsedge_series {
    char* name;
    char* dir_path;
    uint32_t active_segment_id;
    char* active_segment_path;
    uint32_t* segment_ids;
    size_t segment_count;
    size_t segment_capacity;

    /*
     * Recent appends are accumulated in memory so small writes can be grouped
     * into fixed-size compressed blocks. WAL keeps these buffered points
     * recoverable until they are flushed to a segment.
     */
    tsedge_point* buffer;
    size_t buffer_count;
    size_t buffer_capacity;

    /*
     * Volatile block index built from segment metadata on open. It is not part
     * of the public API and can be rebuilt from the segment file at any time.
     */
    tsedge_block_index_entry* block_index;
    size_t block_index_count;
    size_t block_index_capacity;
} tsedge_series;

int tsedge_series_validate_name(const char* name);
int tsedge_series_init(tsedge_series* series, const char* series_dir, const char* name, bool create_dir);
void tsedge_series_free(tsedge_series* series);
int tsedge_series_append(struct tsedge_db* db, tsedge_series* series, int64_t timestamp, double value);
int tsedge_series_append_batch(struct tsedge_db* db, tsedge_series* series, const tsedge_point* points, size_t count);
int tsedge_series_add_recovered_point(struct tsedge_db* db, tsedge_series* series, const tsedge_point* point);

/* Converts the current buffer into one compressed block in the segment file. */
int tsedge_series_flush(struct tsedge_db* db, tsedge_series* series, bool update_wal);

#endif
