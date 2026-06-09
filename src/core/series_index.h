#ifndef TSEDGE_SERIES_INDEX_H
#define TSEDGE_SERIES_INDEX_H

#include "series.h"

int tsedge_series_add_segment_id(tsedge_series* series, uint32_t segment_id);
int tsedge_series_add_block_index_entry(tsedge_series* series, const tsedge_block_index_entry* entry);
int tsedge_series_rebuild_index(tsedge_series* series);
void tsedge_series_clear_index(tsedge_series* series);

#endif
