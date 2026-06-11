# Benchmarking

Benchmarks are used to measure whether TSEdge is suitable for local numeric
time-series storage on edge devices and to provide reproducible numbers for the
diploma benchmark chapter.

## Comparison Targets

The benchmark chapter may compare:

- TSEdge.
- Raw binary files with `int64 timestamp` and `double value`.
- CSV files.
- SQLite, if it is available and a separate comparison benchmark is added.

`tsedge_bench` measures TSEdge directly and includes both single-point appends
and batch appends as write modes of the same system. `file_bench` measures raw
binary and CSV files. `sqlite_bench` is built only when SQLite3 is available.
`tsedge_write_bench` focuses only on the TSEdge write path and separates append,
flush and close time.

## Datasets

The benchmark uses synthetic datasets:

- `smooth`: slowly changing values with small noise.
- `noisy`: random values.
- `step`: values that stay constant for a range of points, then change.
- `constant`: identical values, useful for showing the best case for XOR value
  compression.
- `irregular_timestamps`: non-uniform timestamp intervals, useful for showing
  delta-of-delta behavior when sampling is not perfectly regular.

## Metrics

Useful benchmark metrics include:

- `write_seconds`
- `write_points_per_sec`
- `read_seconds`
- `read_points_per_sec`
- `avg_seconds`
- `size_bytes`
- `raw_size_bytes`
- `compression_ratio`
- `segment_count`
- `block_count`
- `total_segment_size_bytes`
- Optional scan counters such as `decoded_block_count` and
  `skipped_block_count` may be added later; notebooks tolerate their absence.

The current benchmark output includes throughput, aggregate time, database size,
raw binary size, compression ratio, number of segment files, number of persisted
blocks, and total segment-file bytes.

## Running

Build the project first:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

Run the benchmark:

```bash
./tsedge_bench 1000000
./file_bench 1000000
./sqlite_bench 1000000
```

Use a smaller value, such as `1000`, for a quick smoke test.

Each benchmark can also write a machine-readable CSV result file:

```bash
./tsedge_bench 1000000 results_tsedge.csv
./file_bench 1000000 results_file.csv
./sqlite_bench 1000000 results_sqlite.csv
```

`tsedge_bench` reports `write_mode` and `batch_size` for TSEdge rows. It keeps
`system=tsedge` for both modes and compares `batch_size=1` using
`tsedge_append` with batch sizes `100`, `1000`, and `4096` using
`tsedge_append_batch`.

## Write Path Benchmarks

`tsedge_write_bench` measures write throughput before doing optimization work.
It uses the public API only and writes deterministic timestamp/value points.

Example commands:

```bash
./build/tsedge_write_bench --points 1000000 --mode append --series 1
./build/tsedge_write_bench --points 1000000 --mode append --series 1 --durability fast
./build/tsedge_write_bench --points 1000000 --mode append --series 1 --durability balanced
./build/tsedge_write_bench --points 1000000 --mode append --series 1 --durability strict
./build/tsedge_write_bench --points 1000000 --mode append_handle --series 1
./build/tsedge_write_bench --points 1000000 --mode batch --batch-size 1000 --series 1
./build/tsedge_write_bench --points 1000000 --mode batch --batch-size 1000 --series 1 --durability fast
./build/tsedge_write_bench --points 1000000 --mode batch_handle --batch-size 1000 --series 1
./build/tsedge_write_bench --points 1000000 --mode batch --batch-size 1000 --series 6
```

The helper script runs the common cases:

```bash
sh bench/run_write_benchmarks.sh
```

Useful fields:

- `append_seconds`: time spent in `tsedge_append` or `tsedge_append_batch`.
- `durability`: WAL flushing policy used by the run.
- `flush_seconds`: time spent in explicit `tsedge_flush_all`.
- `close_seconds`: time spent in `tsedge_close` after the flush.
- `elapsed_seconds`: append, flush and close time together.
- `points_per_second`: throughput over `elapsed_seconds`.
- `append_points_per_second`: throughput of the append loop alone.
- `wal_size_bytes`: bytes already written to `wal.log` before explicit flush.
- `database_size_bytes`: total size of the database directory.
- `compression_ratio`: raw point bytes divided by segment bytes on disk.
- `bytes_per_point`: segment bytes divided by written points.

Use these numbers to decide where to optimize next: WAL append, batch path,
flush/compression, series lookup, or filesystem syncing.
Compare `append` with `append_handle` to estimate the cost of looking up a
series by name for every point.

Batch writes use a WAL batch record: the series name and checksum are stored
once for a group of points. This lowers WAL overhead and makes
`wal_size_bytes` easier to compare between batch sizes.

## Read Path Benchmarks

`tsedge_read_bench` generates a database, flushes it, closes it, opens it again,
and then measures only read/query time. This shows the cost of range reads,
aggregate queries, block-index filtering and block-level aggregate metadata.

Example commands:

```bash
./build/tsedge_read_bench --points 1000000 --series 1 --scenario all --repeat 5
./build/tsedge_read_bench --points 1000000 --series 1 --scenario read_range_small --range-size 1000 --repeat 10
./build/tsedge_read_bench --points 1000000 --series 6 --scenario aggregate_avg_full --repeat 5
```

The helper script runs the common read cases:

```bash
sh bench/run_read_benchmarks.sh
```

Implemented scenarios:

- `read_range_tiny`, `read_range_small`, `read_range_medium`,
  `read_range_large`, `read_range_full`
- `aggregate_avg_tiny`, `aggregate_avg_medium`, `aggregate_avg_full`
- `aggregate_min_max_tiny`, `aggregate_min_max_medium`,
  `aggregate_min_max_full`
- `window_aggregate`

Useful fields:

- `query_seconds`: average measured query time, excluding database generation.
- `points_read`: points delivered through the range-read callback.
- `value_sum`: checksum-like sum of values seen by `read_range`.
- `result_value`, `result_min`, `result_max`: aggregate results.
- `blocks_total`: blocks in the queried series set.
- `blocks_scanned`: block-index entries checked against the timestamp range.
- `blocks_skipped`: blocks skipped by timestamp metadata.
- `blocks_decoded`: blocks whose payload was decompressed.
- `points_decoded`: points decompressed from payload blocks.

For fully covered aggregate blocks, `blocks_decoded` stays low because the query
uses block-level min/max/sum/count metadata instead of decompressing payload.
`aggregate_min_max_*` uses one internal aggregate pass, so block counters are not
double-counted.

## Window Aggregation Benchmark

Window aggregation measures downsampling for graph and overview queries. Instead
of returning every raw point, `tsedge_aggregate_windowed` returns non-empty
windows with count, min, max and avg.

Example commands:

```bash
./build/tsedge_read_bench --points 1000000 --series 1 --scenario window_aggregate --target-windows 100 --repeat 5
./build/tsedge_read_bench --points 1000000 --series 1 --scenario window_aggregate --target-windows 1000 --repeat 5
./build/tsedge_read_bench --points 1000000 --series 1 --scenario window_aggregate --target-windows 10000 --repeat 5
./build/tsedge_read_bench --points 1000000 --series 6 --scenario window_aggregate --target-windows 1000 --repeat 5
```

The same focused set can be run with:

```bash
./bench/run_window_benchmarks.sh
```

Useful fields:

- `window_size`: timestamp width of each half-open window.
- `target_windows`: requested number of output windows when the benchmark
  derives `window_size`.
- `window_count`: number of non-empty windows returned.
- `windows_per_second`: produced windows per second.
- `source_points_per_second`: source points summarized per second.
- `first_window_*` and `last_window_*`: sample output buckets for sanity checks.
- `blocks_decoded`: blocks that had to be decompressed.
- `points_decoded`: source points decompressed from those blocks.

When block metadata can satisfy a window, decoded counters stay low. Blocks that
cross a window boundary are decoded and distributed point by point.

## Block Size Tuning

Block size controls how many points are compressed into one block. The default
is `16384` points and can be changed at configure time:

```bash
cmake -S . -B build-bs-8192 -DCMAKE_BUILD_TYPE=Release -DTSEDGE_BLOCK_MAX_POINTS=8192
cmake --build build-bs-8192
```

The helper script compares common block sizes:

```bash
sh bench/run_block_size_benchmarks.sh
```

It builds and benchmarks:

```text
1024, 4096, 8192, 16384, 32768
```

Small blocks reduce extra decoding for tiny and small ranges. Large blocks
reduce block/header overhead and can improve full scans or compression, but a
small range may decode more points than it returns.

Recent tuning results for the current benchmark workload:

| Block size, points | Batch fast, points/sec | Append fast, points/sec | Tiny read, sec | Small read, sec | Full read, points/sec | AVG full, sec | MIN/MAX full, sec | Compression ratio | Bytes/point | Block count |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024 | 5.14M | 3.06M | 0.000021 | 0.000043 | 47.35M | 0.013701 | 0.013135 | 1.5868 | 10.083 | 977 |
| 4096 | 12.81M | 6.82M | 0.000039 | 0.000077 | 83.12M | 0.003304 | 0.003385 | 1.5967 | 10.021 | 245 |
| 8192 | 16.12M | 7.90M | 0.000057 | 0.000111 | 103.26M | 0.001582 | 0.001677 | 1.5983 | 10.010 | 123 |
| 16384 | 16.45M | 8.25M | 0.000091 | 0.000092 | 115.31M | 0.000830 | 0.000795 | 1.5992 | 10.005 | 62 |
| 32768 | 17.70M | 6.12M | 0.000170 | 0.000175 | 121.70M | 0.000439 | 0.000412 | 1.5996 | 10.003 | 31 |

The default block size was changed from `4096` to `16384` points.

A 16384-point block provides the best overall trade-off for the current
workload:

- Batch writes remain close to the best result.
- Single append throughput is the best among tested values.
- Full-range reads improve significantly compared with 4096.
- Full-range aggregates become faster because fewer block metadata records need
  to be scanned.
- Compression ratio and bytes per point improve slightly.
- Tiny and small range reads remain fast enough, while 32768 starts to penalize
  them more noticeably.

The 32768-point block is faster for full scans and full aggregates, but it
decodes too many extra points for small interactive range queries. The
1024-point block is better for minimal over-read, but creates too many blocks
and hurts write, full-read and aggregate performance.

These numbers are workload- and machine-dependent, but they justify the current
default for the benchmarked edge time-series workload.

The script writes a summary CSV to:

```text
bench/block_size_results.csv
```

Important fields:

- `read_tiny_query_seconds`
- `read_small_query_seconds`
- `read_full_points_per_second`
- `aggregate_avg_full_query_seconds`
- `aggregate_min_max_full_query_seconds`
- `compression_ratio`
- `bytes_per_point`
- `block_count`
- `tiny_points_decoded`
- `small_points_decoded`

With the default 64 MiB segment limit, small benchmark databases may still fit
into one segment. To demonstrate rotation with a small point count, configure a
separate build with a smaller limit:

```bash
cmake -DTSEDGE_SEGMENT_MAX_BYTES=8192 ..
cmake --build .
```

From the repository root, the helper script writes the expected notebook input
files into `benchmark_results/`:

```bash
sh scripts/run_benchmarks.sh 1000000
```

## Notebook Environment

The benchmark analysis notebook uses Python packages listed in
`requirements.txt`:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
jupyter notebook notebooks/benchmark_analysis.ipynb
```

The `.venv/` directory and generated `benchmark_results/` files are ignored by
Git.

## Using Results

Benchmark output is line-oriented and easy to copy into a diploma document:

```text
dataset=smooth
points=1000000
write_points_per_sec=...
read_points_per_sec=...
db_size_bytes=...
raw_size_bytes=16000000
compression_ratio=...
segment_count=...
block_count=...
total_segment_size_bytes=...
```

Always record the device, compiler, build type, storage medium, operating
system, and point count near the results. Benchmark numbers depend on the
device, compiler, filesystem, and storage hardware.
