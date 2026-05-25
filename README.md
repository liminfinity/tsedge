# TSEdge

TSEdge is a small embedded C11 time-series storage library for Linux edge
devices. Applications include `tsedge.h`, link with `libtsedge`, and call the
API directly. It is not a server and does not implement SQL, networking, MQTT,
replication, access control, or lossy compression.

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
ctest
```

More build details are in [docs/build.md](docs/build.md).

The build produces:

- `libtsedge.so`
- `libtsedge.a`
- `tsedge_demo`
- `tsedge_bench`
- `tsedge_tests`

## API

The public API lives in `include/tsedge.h` and exposes an opaque `tsedge_db`.
The first version supports:

- open/close database directory
- create series
- append `(int64 timestamp, double value)` points
- read points by inclusive time range
- aggregate min/max/sum/avg/count by range
- export a range to CSV

Function-level API notes are in [docs/api.md](docs/api.md).

## Storage Layout

TSEdge stores data on the local filesystem:

```text
database_dir/
  manifest.txt
  wal.log
  series/
    motor.temperature/
      metadata.txt
      segment_000001.tse
```

`manifest.txt` lists known series. Each series has one append-only segment file
in this prototype. `wal.log` stores not-yet-flushed points for crash recovery.
After a successful block flush, the WAL is rewritten from current in-memory
buffers so already persisted blocks are not replayed twice.

## Segment Format

Segment files contain a sequence of blocks. Multi-byte integers are encoded
explicitly as little-endian values.

Each block starts with a 48-byte header:

```text
u32 magic              "TSEB" as 0x42455354
u32 version            1
u32 point_count
u32 compression_type   1 for delta timestamps + XOR values
i64 min_timestamp
i64 max_timestamp
u32 compressed_timestamp_size
u32 compressed_value_size
u32 payload_size
u32 reserved           0
```

The header is followed by compressed timestamp bytes and compressed value bytes.
The min/max timestamp metadata allows range queries to skip blocks that do not
intersect the requested interval.

## Compression

Compression is lossless.

Timestamps are encoded as:

- first timestamp as raw `int64`
- first delta as raw `int64`
- subsequent delta-of-delta values as zigzag varints

Double values are encoded as:

- first value as raw IEEE-754 64-bit bits
- each next value as a marker:
  - `0` when XOR with previous value is zero
  - `1` followed by the raw 64-bit XOR otherwise

This is a simplified Gorilla-inspired XOR stream. It favors correctness and
explainability over maximum compression ratio.

## Benchmark

```bash
./tsedge_bench 1000000
```

Benchmark methodology is described in [docs/benchmarking.md](docs/benchmarking.md).

The benchmark emits copy-friendly lines for smooth, noisy, and step-like
datasets:

```text
dataset=smooth
points=1000000
write_points_per_sec=...
read_points_per_sec=...
aggregate_seconds=...
db_size_bytes=...
raw_size_bytes=16000000
compression_ratio=...
```
