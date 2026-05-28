#ifndef TSEDGE_CSV_H
#define TSEDGE_CSV_H

#include "tsedge.h"

/*
 * CSV export is an external interchange path. It reads decoded points through
 * the public range API and writes ordinary timestamp,value rows; the compressed
 * block format remains an internal storage detail.
 */
int tsedge_csv_export(tsedge_db* db, const char* series_name, int64_t from_timestamp, int64_t to_timestamp, const char* output_path);

#endif
