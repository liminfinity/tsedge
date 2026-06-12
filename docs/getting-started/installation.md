# Installation

## Python package

Install the published Python package from PyPI:

```bash
pip install tsedge
```

The published wheels are bundled platform wheels. They include the native TSEdge shared library inside the Python package, so `TSEDGE_LIBRARY` is not required for normal installation.

Check that the package imports:

```bash
python -c "from tsedge import TSEdge; print('TSEdge import OK')"
```

## Native library override

`TSEDGE_LIBRARY` remains available as an override mechanism when you want to use a manually built native library:

```bash
export TSEDGE_LIBRARY=/path/to/libtsedge.so
```

On macOS, use the `.dylib` file:

```bash
export TSEDGE_LIBRARY=/path/to/libtsedge.dylib
```

You can also pass an explicit library path in Python:

```python
from tsedge import TSEdge

db = TSEdge.open("demo_db", lib_path="/path/to/libtsedge.so")
db.close()
```

## Source distribution

An sdist is not published yet. Source-build installation should be configured separately because the Python package relies on the native C library.

## Windows

Windows wheels are not included in the current release.
