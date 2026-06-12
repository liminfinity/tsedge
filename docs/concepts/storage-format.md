# Storage Format

TSEdge stores data in ordinary files under a database directory. The layout is intentionally simple so it can be inspected, tested and explained without a separate server process.

```text
database_dir/
  manifest.txt
  wal.log
  series/
    air.temperature/
      metadata.txt
      segment_000001.tse
      segment_000002.tse
```

## Files

- `manifest.txt` marks the directory as a TSEdge database and stores basic database metadata.
- `wal.log` stores accepted points that have not yet been flushed into segment blocks.
- `metadata.txt` stores per-series metadata.
- `segment_*.tse` files store append-only compressed blocks.

## Segments and blocks

Each segment file is an append-only sequence of compressed blocks. A block is the unit of compression and query skipping.

Blocks contain enough metadata to answer a simple question quickly: can this block contain points in the requested time range? If not, the reader can skip the compressed payload and move to the next block.

The payload contains two compressed streams:

- timestamp stream
- value stream

Multi-byte integer fields are encoded explicitly in little-endian order. This avoids silently depending on the host CPU byte order.

## Block header

| Field | Type | Purpose |
|---|---|---|
| magic | u32 | block signature |
| version | u32 | block format version |
| point_count | u32 | number of points |
| min_timestamp | i64 | block time lower bound |
| max_timestamp | i64 | block time upper bound |
| timestamp_size | u32 | compressed timestamp stream size |
| value_size | u32 | compressed value stream size |
| payload_size | u32 | total compressed payload size |
| min_value | f64 | minimum value |
| max_value | f64 | maximum value |
| sum_value | f64 | sum of values |

The current implementation also stores a compression type and a reserved field in the binary block header. Readers validate these fields before decoding so malformed files fail cleanly.

Block-level `min_value`, `max_value`, `sum_value` and `point_count` allow fully covered aggregate queries to avoid decompressing payloads.
