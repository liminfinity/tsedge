# Release Notes

## v0.1.1

First public release of TSEdge as both a native C library and a Python package.

This release focuses on the core embedded storage story: local append-oriented time-series files, crash recovery, range queries, aggregate queries and bundled Python wheels.

### Added

- Embedded C11 time-series storage library.
- Compressed segment files.
- WAL recovery.
- Range reads and aggregate queries.
- Windowed aggregation.
- Batch writes.
- Disk quota support.
- Database verification.
- CSV export.
- Python binding based on `ctypes`.
- PyPI wheels for Linux x86_64, Linux aarch64, macOS arm64 and macOS x86_64.

### Notes

- Windows wheels are not included.
- sdist is not published yet.
- Published Python wheels include the native library, so `TSEDGE_LIBRARY` is not required for normal installation.
