# Python Examples

Example scripts live in `python/examples/`. They are intentionally small and use the public Python package, so they are a good way to learn the API before reading the C implementation.

Run them from the `python/` directory after installing a bundled wheel:

```bash
cd python
python examples/basic_usage.py
python examples/batch_write.py
python examples/read_and_aggregate.py
python examples/window_aggregate.py
python examples/sensor_simulation.py
```

When running from the source tree without an installed bundled wheel, build the native library first and set `TSEDGE_LIBRARY`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

PYTHONPATH=python/src TSEDGE_LIBRARY=$PWD/build/libtsedge.so \
python3 python/examples/basic_usage.py
```

On macOS, use `libtsedge.dylib`:

```bash
PYTHONPATH=python/src TSEDGE_LIBRARY=$PWD/build/libtsedge.dylib \
python3 python/examples/basic_usage.py
```

## Available examples

- `basic_usage.py`: the shortest end-to-end example.
- `batch_write.py`: batch ingestion for larger point sets.
- `read_and_aggregate.py`: range reads and aggregate queries.
- `window_aggregate.py`: compact buckets for chart-style views.
- `sensor_simulation.py`: a more realistic multi-series sensor workflow with CSV export.

If you are new to the project, start with `basic_usage.py`, then move to `sensor_simulation.py`.
