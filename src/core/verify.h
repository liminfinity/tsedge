#ifndef TSEDGE_VERIFY_H
#define TSEDGE_VERIFY_H

#include "tsedge.h"

/* Scans database files and reports the first structural integrity problem. */
int tsedge_verify_internal(const char* db_path, tsedge_verify_report* report);

#endif
