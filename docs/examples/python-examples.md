# Python Examples

Example scripts live in `python/examples/`.

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

- `basic_usage.py`: open a database, create a series, write points and query aggregates.
- `batch_write.py`: write many points in batches.
- `read_and_aggregate.py`: read ranges and compute aggregates.
- `window_aggregate.py`: produce compact windows for chart-style queries.
- `sensor_simulation.py`: simulate multiple sensor series and export CSV.
