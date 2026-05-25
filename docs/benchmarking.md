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

The current `tsedge_bench` program measures TSEdge directly and reports raw
binary size for compression-ratio comparison.

## Datasets

The benchmark uses synthetic datasets:

- `smooth`: slowly changing values with small noise.
- `noisy`: random values.
- `step`: values that stay constant for a range of points, then change.

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

The current benchmark output includes throughput, aggregate time, database size,
raw binary size, and compression ratio.

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
```

Use a smaller value, such as `1000`, for a quick smoke test.

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
```

Always record the device, compiler, build type, storage medium, operating
system, and point count near the results. Benchmark numbers depend on the
device, compiler, filesystem, and storage hardware.
