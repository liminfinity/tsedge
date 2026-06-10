#ifndef TSEDGE_SERIES_INDEX_H
#define TSEDGE_SERIES_INDEX_H

#include "series.h"

/* Records a segment id discovered or created for a series. */
int tsedge_series_add_segment_id(tsedge_series* series, uint32_t segment_id);

/* Adds one block location to the volatile range index. */
int tsedge_series_add_block_index_entry(tsedge_series* series, const tsedge_block_index_entry* entry);

/* Rebuilds the volatile block index from segment files on disk. */
int tsedge_series_rebuild_index(tsedge_series* series);

/* Clears in-memory segment and block index arrays. */
void tsedge_series_clear_index(tsedge_series* series);

#endif
