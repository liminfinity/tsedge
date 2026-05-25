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

The current CMake project does not define install rules yet, so
`cmake --install` is not available as a supported workflow. If install rules are
added later, the expected command will be:

```bash
cmake --install .
```
