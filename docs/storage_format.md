# Storage Format

TSEdge is an embedded storage engine. Applications link the library and data is
stored in files under the database directory; there is no server, socket, SQL
layer, or network protocol.

## Layout

```text
database_dir/
  manifest.txt
  wal.log
  series/
    motor.temperature/
      metadata.txt
      segment_000001.tse
```

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
