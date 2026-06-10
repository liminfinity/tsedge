#ifndef TSEDGE_SERIES_DELETE_H
#define TSEDGE_SERIES_DELETE_H

#include "db.h"

/* Deletes one loaded series and removes its files from disk. */
int tsedge_db_delete_series(tsedge_db* db, const char* series_name);

#endif
