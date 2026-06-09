#ifndef TSEDGE_DB_H
#define TSEDGE_DB_H

#include "series.h"
#include "tsedge.h"

#include <stddef.h>

/**
 * Internal database state for the embedded storage engine.
 *
 * There is no server process behind this object: the application owns the
 * handle, and all filesystem work happens inside the caller's process.
 */
struct tsedge_db {
    char* path;
    char* series_dir;
    char* wal_path;

    /* Dynamic registry of known series loaded from the database directory. */
    tsedge_series* series;
    size_t series_count;
    size_t series_capacity;
};

char* tsedge_path_join(const char* a, const char* b);

/*
 * Creates a directory when missing and accepts an existing directory. This is
 * used during database and series initialization.
 */
int tsedge_mkdir_if_needed(const char* path);

/* Opens the database directory, loads series, then replays WAL recovery. */
int tsedge_db_open_internal(const char* path, tsedge_db** out_db);

/* Flushes all series before releasing the embedded database handle. */
int tsedge_db_close_internal(tsedge_db* db);
int tsedge_db_create_series_internal(tsedge_db* db, const char* name);
tsedge_series* tsedge_db_find_series(tsedge_db* db, const char* name);
int tsedge_db_add_series_object(tsedge_db* db, const char* name, int create_dir);

/* Flushes all in-memory buffers and leaves WAL containing only unflushed data. */
int tsedge_db_flush_all(tsedge_db* db);
int tsedge_db_rewrite_manifest(tsedge_db* db);
void tsedge_db_free_memory(tsedge_db* db);

#endif
