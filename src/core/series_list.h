#ifndef TSEDGE_SERIES_LIST_H
#define TSEDGE_SERIES_LIST_H

#include "db.h"

/* Copies the loaded database series registry into a caller-owned array. */
int tsedge_db_list_series(tsedge_db* db, tsedge_series_info** out_series, size_t* out_count);

#endif
