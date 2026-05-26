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

## Run Demo

From the build directory:

```bash
./tsedge_demo
```

The demo creates `demo_db/`, appends sample points, computes an average, and
exports `temperature.csv`.

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
