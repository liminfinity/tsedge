# TSEdge

TSEdge is an embedded C11 time-series storage library designed for local data collection on edge devices.

It provides append-oriented storage, compressed segment files, write-ahead logging, range reads, aggregate queries, windowed aggregation, CSV export and a Python binding.

## What TSEdge is

- Embedded C library.
- Local time-series storage.
- Append-oriented storage engine.
- Designed for Linux/POSIX edge devices.
- Usable from C and Python.

## What TSEdge is not

- Not a SQL database.
- Not a network server.
- Not a distributed storage system.
- Not a Prometheus or InfluxDB replacement.
- Not a multi-writer database.

## Quick install

```bash
pip install tsedge
```

## Supported prebuilt wheels

- Linux x86_64
- Linux aarch64
- macOS arm64
- macOS x86_64

Windows wheels are not included in the current release.

## Links

- PyPI: https://pypi.org/project/tsedge/
- GitHub: https://github.com/liminfinity/tsedge
- Release v0.1.1: https://github.com/liminfinity/tsedge/releases/tag/v0.1.1
