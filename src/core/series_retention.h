#ifndef TSEDGE_SERIES_RETENTION_H
#define TSEDGE_SERIES_RETENTION_H

#include "series.h"

int tsedge_series_delete_before(struct tsedge_db* db, tsedge_series* series, int64_t older_than_timestamp);

#endif
