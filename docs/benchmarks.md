# Benchmarks

TSEdge benchmarks measure whether the engine is suitable for local numeric time-series storage on edge devices.

## Setup

Common benchmark runs use 1,000,000 points.

Datasets:

- `smooth`: slowly changing values with small noise.
- `noisy`: random values.
- `step`: values that stay constant for a range of points, then change.
- `constant`: identical values.
- `irregular_timestamps`: non-uniform timestamp intervals.

Systems:

- TSEdge.
- Raw binary files.
- CSV files.
- SQLite, when SQLite support is available during the build.

Metrics:

- storage size
- compression ratio
- write throughput
- read throughput
- AVG aggregate time

## Running

```bash
mkdir build
cd build
cmake ..
cmake --build .

./tsedge_bench 1000000
./file_bench 1000000
./sqlite_bench 1000000
```

The benchmark programs can also write CSV result files for analysis.

## Block-size tuning results

The following table is copied from the current repository benchmark notes. It reflects the benchmark workload used when the default block size was selected.

| Block size, points | Batch fast, points/sec | Append fast, points/sec | Tiny read, sec | Small read, sec | Full read, points/sec | AVG full, sec | MIN/MAX full, sec | Compression ratio | Bytes/point | Block count |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024 | 5.14M | 3.06M | 0.000021 | 0.000043 | 47.35M | 0.013701 | 0.013135 | 1.5868 | 10.083 | 977 |
| 4096 | 12.81M | 6.82M | 0.000039 | 0.000077 | 83.12M | 0.003304 | 0.003385 | 1.5967 | 10.021 | 245 |
| 8192 | 16.12M | 7.90M | 0.000057 | 0.000111 | 103.26M | 0.001582 | 0.001677 | 1.5983 | 10.010 | 123 |
| 16384 | 16.45M | 8.25M | 0.000091 | 0.000092 | 115.31M | 0.000830 | 0.000795 | 1.5992 | 10.005 | 62 |
| 32768 | 17.70M | 6.12M | 0.000170 | 0.000175 | 121.70M | 0.000439 | 0.000412 | 1.5996 | 10.003 | 31 |

The default block size is `16384` points.

Additional benchmark details are available in the repository's legacy benchmark notes and scripts.
