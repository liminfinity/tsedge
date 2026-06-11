# Storage Format

TSEdge is an embedded storage engine. Applications link the library and data is
stored in files under the database directory; there is no server, socket, SQL
layer, or network protocol.

## Module Architecture

The source tree is grouped by layer. `src/api` contains the public facade that
validates arguments and delegates to internal modules. `src/core` owns database
and series coordination, including the in-memory block index, range/aggregate
queries, statistics and retention. `src/storage` owns segment files, block
metadata, WAL and segment rotation. `src/compression` contains timestamp/value
compression and primitive byte encoding. `src/export` contains CSV export.

Segment file operations are separated from series logic. `segment_files.c`
formats and parses `segment_%06u.tse` filenames, discovers segment ids and
reports sizes. `segment_rotation.c` decides when the active segment should move
to the next file. `segment.c` reads and writes compressed blocks inside a
concrete segment file.

## Layout

```text
database_dir/
  manifest.txt
  wal.log
  series/
    motor.temperature/
      metadata.txt
      segment_000001.tse
      segment_000002.tse
      segment_000003.tse
```

Each series may contain multiple segment files named `segment_%06u.tse`.
Existing databases that only have `segment_000001.tse` remain valid. New segment
files are created only when a block is flushed and the active segment already
contains data that would exceed the size limit after appending the next block.
The default limit is 64 MiB (`TSEDGE_SEGMENT_MAX_BYTES`).

Rotation happens only between blocks. TSEdge does not split a compressed block
across files. Old data can be removed later at whole-segment granularity, but
partially obsolete segments are not rewritten because compaction is outside the
current prototype.

## Block Header

Each segment file is an append-only sequence of compressed blocks. Multi-byte
integer fields are encoded explicitly as little-endian values.

Current block format:

```text
u32 magic              "TSEB" as 0x42455354
u32 version            2
u32 point_count
u32 compression_type   1 for delta timestamps + Gorilla-inspired XOR values
i64 min_timestamp
i64 max_timestamp
u32 compressed_timestamp_size
u32 compressed_value_size
u32 payload_size
f64 min_value
f64 max_value
f64 sum_value
u32 reserved           must be 0
```

The reader validates magic, version, point count, timestamp bounds, payload
sizes, supported compression type, and reserved fields. The
`min_timestamp`/`max_timestamp` metadata lets range reads skip blocks that do not
intersect the query interval.

Version 2 also stores block-level aggregate statistics. If an aggregate query
fully covers a block, TSEdge can use `min_value`, `max_value`, `sum_value` and
`point_count` without decompressing that block. Partially overlapping blocks are
still decoded and filtered point by point.

Window aggregation uses the same metadata when a block is fully contained in
one requested window. Blocks that overlap a window boundary or a query boundary
are decoded and their points are distributed into half-open windows
`[window_start, window_end)`. Empty windows are not returned by the public API.

When a database is opened, every series directory is scanned for files matching
`segment_*.tse`. TSEdge reads only block headers and skips payloads to rebuild an
in-memory index. Each index entry stores the segment id, block offset,
timestamp bounds, point count and payload size. Range reads and aggregates use
this index to seek directly to candidate blocks, including blocks stored in
different segment files.

## Segment-Level Retention

`tsedge_delete_before` implements coarse-grained retention using the rotated
segment files. The function first flushes the current series buffer so accepted
points are represented by segment blocks rather than only by WAL entries. It
then groups block-index entries by `segment_id` and computes each segment
timestamp range.

A segment file is deleted only when:

```text
segment_max_timestamp < older_than_timestamp
```

If a segment contains both old and new points, it stays on disk unchanged. TSEdge
does not delete individual points, does not compact partial segments, and does
not rename remaining files. For example, after deleting `segment_000001.tse`,
the next files keep names such as `segment_000002.tse` and `segment_000003.tse`.
After deletion, the in-memory block index is rebuilt from the segment files that
remain on disk, so reads, aggregates, CSV export and statistics no longer refer
to removed files.

The WAL format is not changed by retention. Since the buffer is flushed before
deleting segments, the WAL is rewritten by the normal flush path and does not
replay points from deleted segment files after a successful retention operation.

## Disk Quota Cleanup

`tsedge_set_disk_quota` configures a soft runtime limit for the size of database
files on disk. The value is stored only in the open database handle and is not
written to `manifest.txt`.

Quota cleanup counts regular files under the database directory and tries to
reduce the size by deleting old sealed `segment_*.tse` files. Candidates are
ordered by segment `max_timestamp`, oldest first. TSEdge never removes the
active segment file of a series and never removes the last remaining segment
file of a series, even if the database is still larger than the configured
limit.

After a segment file is deleted, the affected series index is rebuilt from the
remaining files. WAL entries are not used to recreate deleted segment files:
flush and close paths rewrite WAL through the normal buffer-based recovery
logic before quota cleanup is considered complete.

If no more segment files can be safely removed and the database is still above
the limit, cleanup returns `TSEDGE_ERR_QUOTA_EXCEEDED`. The database remains
valid; the quota is simply too small for the current active data, WAL and
metadata.

The current value payload is a simplified, byte-aligned, Gorilla-inspired XOR
stream. It stores the first double as raw bits, then stores exact XOR
differences from the previous value. Repeated values use a one-byte marker;
other values use a significant-byte window when it saves space or a raw XOR
fallback in the worst case. This remains lossless and does not claim to
outperform Gorilla.

## WAL v2 and v3

`wal.log` stores points accepted by `tsedge_append` but not yet flushed into a
segment block. It is a simple recovery log for the diploma prototype, not a full
transaction system.

The WAL file format is unchanged by durability modes. FAST and BALANCED may keep
new WAL entries in memory for a short time before appending them to `wal.log`.
STRICT flushes WAL entries after every append or batch. `tsedge_flush`,
`tsedge_flush_all` and `tsedge_close` flush the WAL buffer before finishing.
Recovery can only replay entries that reached `wal.log`.

WAL v2 point entries are still supported during recovery:

```text
u32 magic              "TSEW" as 0x57455354
u32 version            2
u32 entry_size
u32 series_name_length
i64 timestamp
u64 raw_double_bits
bytes series_name
u32 checksum           FNV-1a over all previous entry bytes
```

New writes use WAL v3. A single-point append writes a `POINT` record:

```text
u32 magic              "TSEW" as 0x57455354
u32 version            3
u32 entry_size
u32 record_type        1 = POINT
u32 series_name_length
i64 timestamp
u64 raw_double_bits
bytes series_name
u32 checksum           FNV-1a over all previous entry bytes
```

Batch append writes a `BATCH` record so the series name and checksum are stored
once for many points:

```text
u32 magic              "TSEW" as 0x57455354
u32 version            3
u32 entry_size
u32 record_type        2 = BATCH
u32 series_name_length
u32 point_count
bytes series_name
repeated point_count times:
  i64 timestamp
  u64 raw_double_bits
u32 checksum           FNV-1a over all previous entry bytes
```

Large batches are split into several WAL batch records. Recovery understands v2
point entries, v3 point entries and v3 batch entries. It validates magic,
version, record type, entry size, series name length, point count and checksum
before replaying a complete record. A torn final entry is ignored, so a crash
during the last WAL write can still recover earlier complete entries. A complete
entry with an invalid checksum is reported as corrupt data.
