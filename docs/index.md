# TSEdge

TSEdge is a small embedded C11 storage engine for numeric time-series data. It is built for applications that collect data locally on Linux edge devices and want a compact library instead of a database server.

Applications link TSEdge directly, append `(timestamp, value)` points, and read or aggregate those points later from local files. The project also ships a Python package based on `ctypes`, so the same storage engine can be used from Python without running a separate service.

## Why use it?

TSEdge is useful when you need predictable local storage for sensor-like data:

- Embedded C library.
- Local files, no daemon.
- Append-oriented writes.
- Compressed segment files.
- Write-ahead log recovery.
- Range reads, aggregates and CSV export.
- Python bindings with bundled native wheels.

## What it is not

TSEdge intentionally keeps the scope narrow:

- It is not a SQL database.
- It is not a network server.
- It is not distributed storage.
- It is not a Prometheus or InfluxDB replacement.
- It is not a multi-writer transactional database.

That narrow scope is deliberate. The goal is a readable embedded engine that can be explained, tested and benchmarked.

## Quick install

```bash
pip install tsedge
```

The published Python wheels include the native TSEdge library, so most users do not need to build `libtsedge` manually.

## Supported prebuilt wheels

- Linux x86_64
- Linux aarch64
- macOS arm64
- macOS x86_64

Windows wheels are not included in the current release.

## Start here

- New Python user: [Installation](getting-started/installation.md) and [Quick Start](getting-started/quickstart.md).
- C user: [C API](api/c-api.md) and [C Example](examples/c-example.md).
- Storage internals: [Architecture](concepts/architecture.md), [Storage Format](concepts/storage-format.md), and [WAL and Durability](concepts/wal-durability.md).
- Performance notes: [Benchmarks](benchmarks.md).

## Links

- PyPI: https://pypi.org/project/tsedge/
- GitHub: https://github.com/liminfinity/tsedge
- Release v0.1.1: https://github.com/liminfinity/tsedge/releases/tag/v0.1.1
