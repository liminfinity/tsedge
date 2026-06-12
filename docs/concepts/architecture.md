# Architecture

TSEdge is arranged like a small embedded storage engine, not a service. The application owns the process, calls the public API, and TSEdge reads and writes files inside the database directory.

```text
Application
  -> tsedge.h public API
    -> core
      -> storage
      -> compression
      -> export
```

## Public API

The public C API lives in `include/tsedge.h`. It exposes opaque database and series handles, status codes, point structures and operations for writing, reading, verification and export.

Internal structs stay private. This keeps the on-disk format and in-memory indexes free to evolve without forcing applications to depend on implementation details.

## Core

The core layer coordinates database and series state. It owns the in-memory buffers, the block index used by queries, series metadata, statistics, retention and quota decisions.

## Storage

The storage layer owns write-ahead log records, segment files, segment rotation, block headers and block readers/writers.

Query paths rely on block metadata from this layer. If a block's timestamp range does not intersect the requested range, TSEdge skips the payload entirely.

## Compression

The compression layer is independent from database and series code. It implements timestamp delta-of-delta compression and lossless XOR-based double compression inspired by Gorilla. The priority is exact round-trip correctness, not claiming a new universal compression algorithm.

## Export

The export layer contains CSV export. It streams points from the query path and writes `timestamp,value` rows without requiring the caller to build a large result array first.
