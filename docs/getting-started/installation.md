# Installation

The easiest way to use TSEdge from Python is to install the published wheel from PyPI.

## Python package

```bash
pip install tsedge
```

The wheels are platform-specific and include the native TSEdge library inside the package. On supported platforms, you can import `tsedge` and open a database without setting `TSEDGE_LIBRARY`.

Check the installation:

```bash
python -c "from tsedge import TSEdge; print('TSEdge import OK')"
```

If that command prints `TSEdge import OK`, the Python package can find its bundled native library.

## Native library override

Most users should not need this. `TSEDGE_LIBRARY` is available for development, debugging, or testing a locally built native library.

```bash
export TSEDGE_LIBRARY=/path/to/libtsedge.so
```

On macOS:

```bash
export TSEDGE_LIBRARY=/path/to/libtsedge.dylib
```

You can also pass an explicit library path in Python:

```python
from tsedge import TSEdge

db = TSEdge.open("demo_db", lib_path="/path/to/libtsedge.so")
db.close()
```

The explicit `lib_path` and `TSEDGE_LIBRARY` override the bundled library.

## Source distribution

An sdist is not published yet. Source-build installation needs a separate native build flow because TSEdge depends on a C library. For now, use the bundled wheels or build the C library from the repository.

## Windows

Windows wheels are not included in the current release.
