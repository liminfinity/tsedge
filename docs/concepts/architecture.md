# Architecture

TSEdge is an embedded library. Applications include the public header, link with the library and call the API directly. There is no server process or network protocol.

```text
Application
  -> tsedge.h public API
    -> core
      -> storage
      -> compression
      -> export
```

## Public API

The public C API lives in `include/tsedge.h`. It exposes opaque database and series handles, status codes, point structures, aggregate types and operations for writing, reading, verification and export.

## Core

The core layer owns database and series coordination. It manages open database state, series metadata, in-memory buffers, block indexes, query execution, statistics, retention and quota operations.

## Storage

The storage layer owns write-ahead log records, segment files, segment rotation, block headers and block readers/writers. Query paths use block metadata to skip blocks outside a requested time range.

## Compression

The compression layer is independent from database and series code. It implements timestamp delta-of-delta compression and lossless XOR-based double compression inspired by Gorilla.

## Export

The export layer contains CSV export. It streams points from the query path and writes `timestamp,value` rows.
