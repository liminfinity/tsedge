#ifndef TSEDGE_DB_QUOTA_H
#define TSEDGE_DB_QUOTA_H

#include "db.h"

/* Enforces the runtime disk quota by deleting old sealed segment files. */
int tsedge_db_enforce_disk_quota(tsedge_db* db);

#endif
