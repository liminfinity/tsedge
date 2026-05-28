# Build

## Requirements

- C compiler with C11 support.
- CMake.
- Optional: `sqlite3` if a separate SQLite comparison benchmark is added later.

The core TSEdge library itself has no external runtime dependencies.

## Build And Test

```bash
mkdir build
cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

On Linux, the shared library is built as `libtsedge.so`.
On macOS, the shared library is built as `libtsedge.dylib`.

The static library target builds `libtsedge.a`.

## Optional WAL fsync

By default, WAL writes use normal buffered filesystem I/O. For stronger
durability during crash experiments, enable compile-time WAL syncing:

```bash
cmake -DTSEDGE_WAL_FSYNC=ON ..
cmake --build .
```

When enabled, each WAL append performs `fflush` and `fsync` where available.
This can improve durability of acknowledged appends, but it may reduce write
throughput in benchmarks.

## Optional Segment Limit Override

The default segment rotation limit is 64 MiB. For tests or demonstrations that
need visible rotation with small datasets, override it at configure time:

```bash
cmake -DTSEDGE_SEGMENT_MAX_BYTES=8192 ..
cmake --build .
```

Rotation still happens only between blocks. A block is never split across
segment files.

## Run Demo

From the build directory:

```bash
./tsedge_demo
```

The demo recreates `demo_db/`, creates several series, writes single-point and
batch data, reads a range, computes aggregates, prints stats, exports
`temperature.csv`, closes the database, and reopens it to check recovery of the
in-memory segment index.

## Run Benchmark

From the build directory:

```bash
./tsedge_bench 1000000
```

The optional argument is the number of generated points per dataset.

## Install

After building, install the library and public header with:

```bash
cmake --install .
```

This installs the shared library, the static library, and `include/tsedge.h`
using the platform's standard CMake install directories.
