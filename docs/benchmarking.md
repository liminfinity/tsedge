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
