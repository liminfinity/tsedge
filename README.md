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

## Release Archive

TSEdge can be packaged as a `.tar.gz` archive for GitHub Releases:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
ctest --output-on-failure
cpack
```

The archive is written into the build directory and contains `include/`, `lib/`,
`bin/`, `docs/`, `README.md`, `INSTALL.md`, and `LICENSE`.

Example use after unpacking:

```bash
tar -xzf tsedge-0.1.0-*.tar.gz
cc app.c -Itsedge-0.1.0-*/include -Ltsedge-0.1.0-*/lib -ltsedge -o app
LD_LIBRARY_PATH=tsedge-0.1.0-*/lib ./app
```

On macOS, use `DYLD_LIBRARY_PATH` for the dynamic library path. More release
details are in [docs/release.md](docs/release.md).

## API

The public API lives in `include/tsedge.h` and exposes an opaque `tsedge_db`.
The first version supports:

- open/close database directory
- create series
- list existing series
- append `(int64 timestamp, double value)` points
- append batches of `tsedge_point` values
- inspect lightweight per-series statistics
- read points by inclusive time range
- aggregate min/max/sum/avg/count by range
- export a range to CSV

Function-level API notes are in [docs/api.md](docs/api.md).

Applications can list known series without scanning database files manually:

```c
tsedge_series_info* series = NULL;
size_t count = 0;

int rc = tsedge_list_series(db, &series, &count);
if (rc == TSEDGE_OK) {
    for (size_t i = 0; i < count; ++i) {
        printf("%s\n", series[i].name);
    }
    tsedge_free_series_list(series);
}
```

The function returns a copied array owned by the caller. Empty databases return
`count = 0` and `series = NULL`.

Batch append avoids repeating public validation and series lookup for every
point:

```c
tsedge_point points[3] = {
    {1710000000000LL, 72.4},
    {1710000001000LL, 72.5},
    {1710000002000LL, 72.6},
};

tsedge_append_batch(db, "motor.temperature", points, 3);
```

If a batch append fails partway through, points accepted before the error may
remain stored. TSEdge does not provide all-or-nothing batch transactions.

Buffered points can be forced to disk before export or shutdown:

```c
tsedge_append(db, "motor.temperature", timestamp, value);
tsedge_flush(db, "motor.temperature");
tsedge_export_csv(db, "motor.temperature", from, to, "temperature.csv");
```

`tsedge_flush` writes one series buffer into a segment file.
`tsedge_flush_all` does the same for every series. Empty buffers are not an
error.

Database files can be checked without modifying them:

```c
tsedge_verify_report report;
int rc = tsedge_verify("demo_db", &report);
if (rc != TSEDGE_OK) {
    printf("corrupt database: %s\n", report.first_error_message);
}
```

`tsedge_verify` checks the database directory, manifest, series metadata,
segment files, block headers, payload bounds and WAL entries. It reports the
first problem found but does not repair files.

Series statistics can be read without decoding segment payloads:

```c
tsedge_series_stats stats;
if (tsedge_get_series_stats(db, "motor.temperature", &stats) == TSEDGE_OK) {
    printf("segments=%zu active=%u blocks=%zu buffered=%zu indexed=%zu bytes=%llu ratio=%.2fx\n",
           stats.segment_count,
           stats.active_segment_id,
           stats.block_count,
           stats.buffered_points,
           stats.total_indexed_points,
           (unsigned long long)stats.total_segment_size_bytes,
           stats.compression_ratio);
}
```

The statistics are collected from the in-memory block index, the current buffer
and all segment file sizes. They also include compression stats: estimated raw
size, bytes stored in segment files, compression ratio and average bytes per
point.

Old data can be removed at segment-file granularity:

```c
tsedge_delete_before(db, "motor.temperature", 1710001000000LL);
```

The function deletes only `segment_*.tse` files whose maximum timestamp is older
than the threshold. If a segment contains both old and new points, it is kept
unchanged because this prototype does not implement compaction or point-level
deletion.

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
      segment_000002.tse
      segment_000003.tse
```

`manifest.txt` lists known series. Each series stores immutable compressed
blocks in append-only `segment_%06u.tse` files. The active segment rotates at a
block boundary when it reaches the internal size limit, which is 64 MiB by
default. `wal.log` stores not-yet-flushed points for crash recovery. After a
successful block flush, the WAL is rewritten from current in-memory buffers so
already persisted blocks are not replayed twice.

## Segment Format

Segment files contain a sequence of blocks. Multi-byte integers are encoded
explicitly as little-endian values.

Each block starts with a version 2 header:

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
u32 reserved           0
```

The header is followed by compressed timestamp bytes and compressed value bytes.
The min/max timestamp metadata allows range queries to skip blocks that do not
intersect the requested interval. The value statistics allow aggregate queries
to use fully covered blocks without decompressing them.

When a database is opened, TSEdge discovers all `segment_*.tse` files for every
series and rebuilds one in-memory block index. Each index entry stores the
segment id and offset of a block, so range reads and aggregates can cross segment
file boundaries without changing the public API.

More storage format details are in [docs/storage_format.md](docs/storage_format.md).

## Module Architecture

TSEdge is split into small internal layers:

- `src/api` contains the public facade: it validates public arguments, finds a
  series, and delegates to internal modules.
- `src/core` owns database and series coordination, including block index
  rebuild, range/aggregate queries, lightweight stats and retention.
- `src/storage` owns segment files, segment rotation, block metadata and WAL.
- `src/compression` owns timestamp/value compression and primitive byte
  encoding.
- `src/export` owns CSV export.

The demo program shows the main API flow: it creates three series, writes data
with both `tsedge_append` and `tsedge_append_batch`, reads a range, computes
aggregates, prints stats, applies segment-level retention, exports CSV, closes
the database, reopens it, and checks that the rebuilt segment index still
exposes the remaining data.

## Crash Recovery Demo

The crash recovery demo uses two small programs. The first writes points and
exits without `tsedge_close`; the second opens the database and checks that WAL
replay restored the points.

```bash
rm -rf crash_recovery_demo_db
./tsedge_crash_writer crash_recovery_demo_db || true
./tsedge_recover_check crash_recovery_demo_db
```

The non-zero exit code from `tsedge_crash_writer` is expected: it intentionally
simulates a crashed process.

## Панель метеопоста и диагностика TSEdge

Интерактивная демонстрация показывает автономный экологический пост:

```text
C-agent -> TSEdge -> live_state.json -> Next.js simulator
```

C-agent пишет реальные точки в TSEdge через публичный API. Сайт не подключается
к базе и не является сервером TSEdge. Он читает файлы состояния C-программы и
пишет команды в `command.json`.

Код demo-agent лежит в `examples/ecopost/` и разделён по ответственности:
конфигурация, состояние, генерация датчиков, команды, операции с TSEdge,
запись `live_state.json` и файловые helper-функции.

Собрать и запустить агент:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DTSEDGE_SEGMENT_MAX_BYTES=8192 ..
cmake --build .
ctest --output-on-failure
./tsedge_ecopost_agent --live --interval-ms 1000
```

Запустить сайт в другом терминале:

```bash
cd demo/system-simulator
npm install
TSEDGE_LIVE_OUTPUT=../../build/ecopost_live_output npm run dev
```

Открыть `http://localhost:3000`. Это пользовательская панель метеопоста:
температура, влажность, давление, ветер, PM2.5, аккумулятор и состояние связи.

Инженерная диагностика доступна на `http://localhost:3000/diagnostics`. Она
показывает WAL, буфер, сжатые blocks, segment-файлы, очистку старых данных и
выгрузку CSV.

## Deleting Old Data

`tsedge_delete_before` implements a simple retention policy on top of segment
rotation. Before deleting files, TSEdge flushes the current in-memory buffer so
the WAL and segment files describe the same accepted points. It then computes
the timestamp range of each segment from the in-memory block index and removes
only segments whose `segment_max_timestamp < older_than_timestamp`.

Partially overlapping segments are preserved completely. Remaining segment files
are not renamed, so after deleting `segment_000001.tse` and
`segment_000002.tse`, later files such as `segment_000003.tse` keep their names
and future rotation continues from the highest existing segment id. After
deletion, the block index is rebuilt from the segment files that are still on
disk. Precise deletion inside a segment would require compaction and is not
implemented in this prototype.

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
  - `1` followed by a byte-aligned significant XOR window when it saves space
  - `2` followed by the raw 64-bit XOR as a worst-case fallback

This is a simplified Gorilla-inspired XOR stream. It favors correctness and
explainability over maximum compression ratio.

## Implemented Optimizations

- Block skipping by timestamp bounds during range reads.
- WAL recovery for not-yet-flushed points.
- WAL v2 entry validation with magic, version, entry size and checksum.
- Optional WAL `fsync` mode through `TSEDGE_WAL_FSYNC`.
- Block-level aggregate statistics for fully covered blocks.
- In-memory block index rebuilt from segment headers at open.
- Segment rotation at block boundaries with `segment_%06u.tse` files.
- Segment-level retention with `tsedge_delete_before`.
- Byte-aligned Gorilla-inspired value XOR encoding with raw fallback.

Planned future optimizations are documented as limitations below where they are
larger than a safe incremental change.

## Limitations

- No SQL parser or query language.
- No network server, HTTP, MQTT, sockets, or replication.
- No concurrent writer support.
- No full ACID transaction system.
- No production-grade WAL checkpointing.
- No disk-based B+Tree index.
- No point-level deletion, compaction, or retention inside partially overlapping
  segment files.
- The in-memory block index is rebuilt on open and is not persisted separately.
- The value compression remains a simplified Gorilla-inspired XOR stream, not full
  Gorilla bit-packing.
- The prototype assumes mostly append-oriented time-series workloads.

## Benchmark

```bash
./tsedge_bench 1000000
```

Benchmark methodology is described in [docs/benchmarking.md](docs/benchmarking.md).

The benchmark emits copy-friendly lines for `smooth`, `noisy`, `step`,
`constant`, and `irregular_timestamps` datasets. For TSEdge it includes both
single-point writes (`write_mode=append`, `batch_size=1`) and batch writes
(`write_mode=append_batch`, with batch sizes such as `100`, `1000`, `4096`):

```text
dataset=smooth
write_mode=append_batch
batch_size=1000
points=1000000
write_points_per_sec=...
read_points_per_sec=...
aggregate_seconds=...
db_size_bytes=...
raw_size_bytes=16000000
compression_ratio=...
segment_count=...
block_count=...
total_segment_size_bytes=...
```
