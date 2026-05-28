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

The current value payload is a simplified, byte-aligned, Gorilla-inspired XOR
stream. It stores the first double as raw bits, then stores exact XOR
differences from the previous value. Repeated values use a one-byte marker;
other values use a significant-byte window when it saves space or a raw XOR
fallback in the worst case. This remains lossless and does not claim to
outperform Gorilla.

## WAL v2

`wal.log` stores points accepted by `tsedge_append` but not yet flushed into a
segment block. It is a simple recovery log for the diploma prototype, not a full
transaction system.

Current WAL entry format:

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

Replay verifies magic, version, entry size, series name length and checksum. A
torn final entry is ignored, so a crash during the last WAL write can still
recover earlier complete entries. A complete entry with an invalid checksum is
reported as corrupt data.
