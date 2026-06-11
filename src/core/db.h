#ifndef TSEDGE_DB_H
#define TSEDGE_DB_H

#include "series.h"
#include "tsedge.h"

#include <stddef.h>
#include <stdint.h>

struct tsedge_series_handle {
    tsedge_db* db;
    char series_name[TSEDGE_MAX_SERIES_NAME + 1u];
    size_t series_index;
    uint64_t generation;
};

typedef struct {
    size_t blocks_total;
    size_t blocks_scanned;
    size_t blocks_skipped;
    size_t blocks_decoded;
    size_t points_decoded;
} tsedge_read_debug_stats;

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
    tsedge_durability_mode durability;
    uint64_t disk_quota_bytes;
    int disk_quota_exceeded;
    uint8_t* wal_buffer;
    size_t wal_buffer_size;
    size_t wal_buffer_capacity;

    /* Dynamic registry of known series loaded from the database directory. */
    tsedge_series* series;
    size_t series_count;
    size_t series_capacity;
    uint64_t next_series_generation;

    /* Database-owned handles returned by the fast append API. */
    tsedge_series_handle** handles;
    size_t handle_count;
    size_t handle_capacity;

    tsedge_read_debug_stats read_debug_stats;
};

/* Allocates a filesystem path by joining two path components. */
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

/* Creates or reuses a series record inside the opened database. */
int tsedge_db_create_series_internal(tsedge_db* db, const char* name);

/* Finds a loaded series by name without creating it. */
tsedge_series* tsedge_db_find_series(tsedge_db* db, const char* name);

/* Returns a db-owned handle for a loaded series. */
int tsedge_db_get_series_handle(tsedge_db* db, const char* series_name, tsedge_series_handle** out_handle);

/* Resolves a handle to the current in-memory series object. */
int tsedge_db_resolve_series_handle(tsedge_db* db, tsedge_series_handle* handle, tsedge_series** out_series);

/* Adds a series object to the in-memory registry and optionally creates files. */
int tsedge_db_add_series_object(tsedge_db* db, const char* name, int create_dir);

/* Flushes all in-memory buffers and leaves WAL containing only unflushed data. */
int tsedge_db_flush_all(tsedge_db* db);

/* Rewrites manifest.txt from the current in-memory series list. */
int tsedge_db_rewrite_manifest(tsedge_db* db);

/* Releases database-owned memory without touching on-disk files. */
void tsedge_db_free_memory(tsedge_db* db);

/* Clears lightweight read-path counters used by benchmarks and tests. */
void tsedge_debug_reset_read_stats(tsedge_db* db);

/* Copies lightweight read-path counters used by benchmarks and tests. */
void tsedge_debug_get_read_stats(tsedge_db* db, tsedge_read_debug_stats* out_stats);

#endif
