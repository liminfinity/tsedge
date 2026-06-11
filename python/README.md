# TSEdge Python Binding

`tsedge` is a Python `ctypes` binding for the TSEdge embedded C time-series
storage library. Python code calls the public C API from `include/tsedge.h`;
the native TSEdge core still owns WAL, segment files, compressed blocks, block
indexes and recovery.

Starting with `0.1.1`, locally built wheels can bundle the native TSEdge shared
library inside the package. If the bundled library is present, `TSEDGE_LIBRARY`
is not required. `TSEDGE_LIBRARY` and explicit `lib_path` arguments still take
priority, so applications can override the bundled library.

The wheel build workflow targets Linux x86_64, Linux aarch64, macOS arm64 and
macOS x86_64. Windows wheels are not included in the current release.

No runtime Python dependencies are required.

## Installation

For local development from the repository:

```bash
cd python
python3 -m pip install .
```

For a built bundled wheel:

```bash
python3 -m pip install python/dist/*.whl
```

When installing from TestPyPI after publishing:

```bash
python3 -m pip install \
  --index-url https://test.pypi.org/simple/ \
  --extra-index-url https://pypi.org/simple/ \
  tsedge
```

## Building the Native Library

Build the C library first:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The Python loader searches in this order:

1. `lib_path` passed to `TSEdge.open(..., lib_path=...)`;
2. `TSEDGE_LIBRARY`;
3. bundled `tsedge/native/libtsedge.dylib`, `libtsedge.so` or `tsedge.dll`;
4. common CMake build directories;
5. the system dynamic-library search path.

Manual override on Linux:

```bash
export TSEDGE_LIBRARY=$PWD/build/libtsedge.so
```

Manual override on macOS:

```bash
export TSEDGE_LIBRARY=$PWD/build/libtsedge.dylib
```

You can also pass a path explicitly:

```python
db = TSEdge.open("sensor_db", lib_path="/path/to/libtsedge.dylib")
```

## Bundled Native Library Wheel

Local macOS bundled wheel build:

```bash
rm -rf build
MACOSX_DEPLOYMENT_TARGET=11.0 cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build
ctest --test-dir build --output-on-failure

cd python
MACOSX_DEPLOYMENT_TARGET=11.0 python3 scripts/prepare_native.py
MACOSX_DEPLOYMENT_TARGET=11.0 python3 -m build --wheel
python3 -m twine check dist/*
```

`scripts/prepare_native.py` copies the compiled native library into:

```text
python/src/tsedge/native/libtsedge.dylib
```

You can override the source library path:

```bash
TSEDGE_NATIVE_LIBRARY=/path/to/libtsedge.dylib python3 scripts/prepare_native.py
```

Check that the wheel is platform-specific and contains the native library:

```bash
ls dist/
python3 -m zipfile -l dist/*.whl | grep tsedge/native
```

Expected local Apple Silicon wheel tag:

```text
tsedge-0.1.1-py3-none-macosx_11_0_arm64.whl
```

The bundled wheel should not be tagged `py3-none-any` and should not use a
CPython ABI tag such as `cp314-cp314`.

Runtime check without `TSEDGE_LIBRARY`:

```bash
python3 -m venv /tmp/tsedge-bundled-test
source /tmp/tsedge-bundled-test/bin/activate
python3 -m pip install python/dist/*.whl
unset TSEDGE_LIBRARY
cd /tmp
python3 -c "from tsedge import TSEdge; db = TSEdge.open('/tmp/tsedge_bundled_db'); db.close(); print('OK')"
deactivate
```

## Building Wheels for Multiple Platforms

Bundled platform wheels are built with GitHub Actions and `cibuildwheel`.
The workflow is defined in:

```text
.github/workflows/build_python_wheels.yml
```

It builds and tests wheels for:

- Linux x86_64 with `tsedge/native/libtsedge.so`;
- Linux aarch64 with `tsedge/native/libtsedge.so`;
- macOS arm64 with `tsedge/native/libtsedge.dylib`;
- macOS x86_64 with `tsedge/native/libtsedge.dylib`.

`python/scripts/build_native.py` runs CMake on each platform, builds the native
`tsedge` shared library and copies it into `python/src/tsedge/native/` before
the wheel is assembled. macOS builds use deployment target 11.0. Windows wheels
are not included in the first bundled release.

The package uses `ctypes`, not a CPython extension. The wheels are therefore
platform-specific but Python-version independent:

```text
py3-none-<platform>
```

`cibuildwheel` is configured to build only `cp310-*` to avoid producing
duplicate wheels with identical `py3-none-<platform>` tags. Package metadata
still declares `requires-python = ">=3.10"`.

Each wheel is tested by `cibuildwheel` after installation. The test clears
`TSEDGE_LIBRARY`, opens a database, writes a batch, reads a range, computes an
AVG aggregate and runs `verify`. This confirms that the installed wheel finds
the bundled native library instead of relying on a local build path.

The workflow uploads wheels as GitHub Actions artifacts. It does not publish to
PyPI automatically; download and inspect artifacts before uploading a release.

Local macOS wheel check:

```bash
rm -rf ../build-python-wheel dist build *.egg-info src/*.egg-info ../wheelhouse
MACOSX_DEPLOYMENT_TARGET=11.0 python scripts/build_native.py
MACOSX_DEPLOYMENT_TARGET=11.0 python -m build --wheel
python -m twine check dist/*
python -m zipfile -l dist/*.whl | grep tsedge/native
```

## Basic Usage

```python
from tsedge import Aggregate, Durability, TSEdge

with TSEdge.open("sensor_db") as db:
    db.create_series("air.temperature")
    db.set_durability(Durability.BALANCED)

    db.append_batch("air.temperature", [
        (1, 23.1),
        (2, 23.4),
        (3, 23.2),
    ])

    avg = db.aggregate("air.temperature", 1, 3, Aggregate.AVG)
    print(avg)
```

## Batch Writes

```python
points = [(i, 20.0 + (i % 100) * 0.01) for i in range(100_000)]
db.append_batch("air.temperature", points)
db.flush_all()
```

The binding accepts `(timestamp, value)` tuples or `Point` dataclass instances.
It builds a temporary `ctypes` array and calls `tsedge_append_batch`.

## Range Reads

`read_range` wraps the C callback API and returns copied `Point` objects:

```python
points = db.read_range("air.temperature", 0, 10_000)
```

The C point pointer is valid only during the callback, so values are copied
immediately.

## Aggregates

```python
avg = db.aggregate("air.temperature", 0, 10_000, "avg")
min_value = db.aggregate("air.temperature", 0, 10_000, "min")
max_value = db.aggregate("air.temperature", 0, 10_000, "max")
count = db.aggregate("air.temperature", 0, 10_000, "count")
```

Aggregate names are case-insensitive. You can also use the `Aggregate` enum.

## Window Aggregation

```python
windows = db.aggregate_windowed(
    "air.temperature",
    start_time=0,
    end_time=1_000_000,
    window_size=1000,
)
```

The binding calls `tsedge_aggregate_windowed`, copies the returned C array into
Python `WindowAggregate` objects and releases the C memory with
`tsedge_free_window_aggregates`.

## Series Metadata

```python
stats = db.get_series_stats("air.temperature")
print(stats.block_count, stats.compression_ratio)

for series in db.list_series():
    print(series.name, series.total_points)
```

`list_series` copies the C array into Python objects and frees it with
`tsedge_free_series_list`.

## Verify

```python
report = db.verify()
print(report.error_count)

report = TSEdge.verify_path("sensor_db")
```

Database corruption reported by `tsedge_verify` is returned in `VerifyReport`.
Set `raise_on_error=True` if you want a `TSEdgeError` instead.

## CSV Export

```python
db.export_csv("air.temperature", 0, 10_000, "temperature.csv")
```

## Disk Quota, If Available

If the loaded C library provides the quota API, the binding exposes it:

```python
db.set_disk_quota(100 * 1024 * 1024)
print(db.get_disk_quota())
db.enforce_disk_quota()
```

If an older native library does not export quota symbols, these methods raise
`TSEdgeError` with a clear message.

## Sensor Simulation Example

`python/examples/sensor_simulation.py` is a realistic edge-device workflow. It
generates deterministic sensor-like telemetry in Python and writes it to TSEdge
in batches. The data is stored by the C core through WAL, segment files and
compressed blocks; Python acts only as the application layer.

The example creates:

- `air.temperature`
- `air.humidity`
- `motor.vibration`
- `motor.current`

Run it from the repository with the source tree:

```bash
PYTHONPATH=python/src TSEDGE_LIBRARY=$PWD/build/libtsedge.dylib \
python3 python/examples/sensor_simulation.py --points 10000 --batch-size 1000
```

After installing a bundled wheel, neither `PYTHONPATH` nor `TSEDGE_LIBRARY` is
needed:

```bash
python3 python/examples/sensor_simulation.py --points 10000 --batch-size 1000
```

Options:

```text
--db
--csv
--points
--batch-size
```

The script prints per-series statistics, min/max/avg/count aggregates, a
60-second window aggregation summary, a small range read, a CSV export path and
a verification report.

## Run Examples From Source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

export TSEDGE_LIBRARY=$PWD/build/libtsedge.dylib
PYTHONPATH=python/src python3 python/examples/basic_usage.py
PYTHONPATH=python/src python3 python/examples/batch_write.py
PYTHONPATH=python/src python3 python/examples/read_and_aggregate.py
PYTHONPATH=python/src python3 python/examples/window_aggregate.py
PYTHONPATH=python/src python3 python/examples/sensor_simulation.py --points 10000 --batch-size 1000
PYTHONPATH=python/src python3 python/smoke_test.py
```

Use `libtsedge.so` in `TSEDGE_LIBRARY` on Linux.

## Build and Check the Package

```bash
cd python
python3 -m pip install --upgrade build twine
MACOSX_DEPLOYMENT_TARGET=11.0 python3 scripts/prepare_native.py
MACOSX_DEPLOYMENT_TARGET=11.0 python3 -m build --wheel
python3 -m twine check dist/*
```

This creates a platform wheel in `python/dist/`.

## Test a Wheel Locally

```bash
python3 -m venv /tmp/tsedge-pkg-test
source /tmp/tsedge-pkg-test/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install python/dist/*.whl
unset TSEDGE_LIBRARY
python3 -c "from tsedge import TSEdge; print(TSEdge)"
python3 python/examples/basic_usage.py
python3 python/examples/sensor_simulation.py --points 1000 --batch-size 100
deactivate
```

## TestPyPI

Do not upload to the main PyPI until the TestPyPI flow has been checked.

Upload to TestPyPI only when credentials are configured:

```bash
cd python
python3 -m twine upload --repository testpypi dist/*
```

Install back from TestPyPI in a clean environment:

```bash
python3 -m venv /tmp/tsedge-testpypi
source /tmp/tsedge-testpypi/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install \
  --index-url https://test.pypi.org/simple/ \
  --extra-index-url https://pypi.org/simple/ \
  tsedge
python3 -c "from tsedge import TSEdge; print(TSEdge)"
deactivate
```

## Limitations

- The bundled wheel workflow targets Linux x86_64, Linux aarch64, macOS arm64
  and macOS x86_64.
- Windows wheels are not included in the current release.
- `read_range` returns a Python list, not a lazy iterator.
- NumPy arrays are not supported in the first version.
