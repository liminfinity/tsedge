#ifndef TSEDGE_WAL_H
#define TSEDGE_WAL_H

#include "db.h"
#include "tsedge.h"

/*
 * Simple write-ahead log for the diploma prototype.
 *
 * Each accepted append is recorded before it enters the in-memory series buffer.
 * This is enough to recover buffered, not-yet-flushed points after a crash, but
 * it is not a full transactional subsystem like an industrial DBMS WAL.
 */
/* Appends one accepted point to the WAL before it enters the buffer. */
int tsedge_wal_append(tsedge_db* db, const char* series_name, const tsedge_point* point);

/* Appends a batch of accepted points to the WAL in series order. */
int tsedge_wal_append_batch(tsedge_db* db, const char* series_name, const tsedge_point* points, size_t count);

/* Replays WAL entries into series buffers during database open. */
int tsedge_wal_replay(tsedge_db* db);

/* Rewrites WAL to contain only points that are still not persisted in blocks. */
int tsedge_wal_truncate_to_buffers(tsedge_db* db);

/* Strictly scans WAL entries and validates entry checksums for verification. */
int tsedge_wal_verify_file(const char* wal_path, size_t* out_entry_count);

#endif
