# AGENTS.md

## Project: TSEdge

TSEdge is a small embedded time-series storage engine written in C.
It is developed as a diploma project prototype for local storage of numeric time-series data on Linux edge devices.

The goal is not to implement a full SQL database and not to invent a new compression algorithm.
The goal is to implement a compact C library for appending, storing, reading, aggregating, compressing and exporting time-series data.

## Main idea

TSEdge is an embedded library, not a server.

External applications do not communicate with it through sockets, HTTP, MQTT or pipes.
They include the public header and link with the library:

```c
#include "tsedge.h"
```

The application calls the library API directly:

```c
tsedge_open(...);
tsedge_create_series(...);
tsedge_append(...);
tsedge_read_range(...);
tsedge_aggregate(...);
tsedge_export_csv(...);
tsedge_close(...);
```

The library stores data in files on the local filesystem.

## Target platform

Primary target:

- Linux
- x86-64 or ARM64
- GCC or Clang
- CMake
- POSIX filesystem API
- dynamic library `.so`
- optional static library `.a`

This project does not target bare-metal microcontrollers.
The device is assumed to have an operating system, filesystem and enough memory to run a normal C application.

## Non-goals

Do not implement:

- SQL parser
- network server
- HTTP API
- MQTT broker/client
- replication
- distributed storage
- multi-user access control
- transactions between multiple series
- complex indexes
- machine learning
- GUI
- encryption
- lossy compression
- support for arbitrary object/blob values

Keep the implementation small, readable and defensible for a diploma project.

## Required features

The first version must implement the following:

1. Open or create a database directory.
2. Create a time series with a name.
3. Append points to a time series.
4. Store each point as:

```text
timestamp: int64
value: double
```

5. Support multiple independent time series.
6. Read points by time range.
7. Compute basic aggregates over a time range:
   - min
   - max
   - avg
   - sum
   - count
8. Export a time range to CSV.
9. Store data persistently on disk.
10. Use a write-ahead log or append-only journal for crash recovery.
11. Use block-based storage.
12. Implement lossless streaming compression:
   - delta-of-delta compression for timestamps
   - XOR-based compression for double values, based on the Gorilla approach
13. Provide tests and benchmark utilities.
14. Build as a library.
15. Provide a small example application.

## Data model

A database contains multiple time series.

A time series has:

```text
name: string
points: ordered sequence of (timestamp, value)
```

For the diploma prototype, one point contains one timestamp and one numeric value.

Do not implement multi-field records in the first version.
If several physical measurements exist, they are represented as different series:

```text
motor.temperature
motor.current
motor.voltage
motor.rpm
motor.vibration_x
motor.vibration_y
```

This keeps the implementation simple and makes compression easier.

## Public C API

Create the public API in:

```text
include/tsedge.h
```

The first version should provide approximately this API:

```c
#ifndef TSEDGE_H
#define TSEDGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tsedge_db tsedge_db;

typedef enum {
    TSEDGE_OK = 0,
    TSEDGE_ERR_INVALID_ARGUMENT = -1,
    TSEDGE_ERR_IO = -2,
    TSEDGE_ERR_NO_MEMORY = -3,
    TSEDGE_ERR_NOT_FOUND = -4,
    TSEDGE_ERR_CORRUPT = -5,
    TSEDGE_ERR_INTERNAL = -6
} tsedge_status;

typedef enum {
    TSEDGE_AGG_MIN,
    TSEDGE_AGG_MAX,
    TSEDGE_AGG_SUM,
    TSEDGE_AGG_AVG,
    TSEDGE_AGG_COUNT
} tsedge_agg_type;

typedef struct {
    int64_t timestamp;
    double value;
} tsedge_point;

typedef int (*tsedge_point_callback)(
    const tsedge_point* point,
    void* user_data
);

int tsedge_open(const char* path, tsedge_db** out_db);
int tsedge_close(tsedge_db* db);

int tsedge_create_series(tsedge_db* db, const char* name);

int tsedge_append(
    tsedge_db* db,
    const char* series_name,
    int64_t timestamp,
    double value
);

int tsedge_read_range(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data
);

int tsedge_aggregate(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_agg_type type,
    double* out_result
);

int tsedge_export_csv(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    const char* output_path
);

const char* tsedge_strerror(int status);

#ifdef __cplusplus
}
#endif

#endif
```

Do not expose internal structs in the public header.

## Storage layout

The database path should contain files similar to:

```text
database_dir/
  manifest.txt
  wal.log
  series/
    motor.temperature/
      metadata.txt
      segment_000001.tse
      segment_000002.tse
    motor.current/
      metadata.txt
      segment_000001.tse
```

The exact format may be simple, but it must be documented in comments and README.

## Segment format

Use block-based storage.

Each segment file may contain several compressed blocks.

A block should store:

```text
magic
version
point_count
min_timestamp
max_timestamp
compressed_timestamp_size
compressed_value_size
compressed_timestamps
compressed_values
checksum optional
```

The exact binary format may be simplified for the first implementation.

Important: every block must contain enough metadata to skip blocks outside a query range.

## Compression

Compression must be lossless.

Timestamp compression:

- Store the first timestamp as int64.
- Store the first delta.
- For each next timestamp:
  - delta = current_timestamp - previous_timestamp
  - delta_of_delta = delta - previous_delta
  - encode delta_of_delta with variable-length signed integer encoding.

Value compression:

- Store the first double as raw 64-bit value.
- For each next double:
  - reinterpret previous and current values as uint64_t
  - xor = previous_bits ^ current_bits
  - if xor == 0, encode a zero marker
  - otherwise encode enough information to reconstruct xor
- Use a simplified Gorilla-like scheme if full Gorilla bit packing is too much.
- Correctness is more important than maximum compression ratio.

Do not use lossy compression.
Decoded points must exactly match the original timestamp and double bit representation.

## WAL and recovery

Implement an append-only WAL:

```text
wal.log
```

Before a point is considered successfully appended, write an entry to WAL.

A WAL entry may contain:

```text
series_name_length
series_name
timestamp
value
```

On `tsedge_open`, replay WAL if needed.

Simplified acceptable approach:

- append writes to WAL
- append also writes to in-memory buffer
- flush converts buffer to compressed block/segment
- after successful flush, WAL may be truncated

Do not overcomplicate transactions.
Crash recovery must be explainable.

## In-memory buffering

Each series should have an in-memory buffer of recently appended points.

When buffer reaches a fixed block size, for example 4096 points, flush it to disk as a compressed block.

Use constants:

```c
#define TSEDGE_BLOCK_MAX_POINTS 4096
```

Keep memory use predictable.

## Query behavior

For `read_range`:

1. Find the requested series.
2. Scan segment metadata.
3. Skip blocks whose `[min_timestamp, max_timestamp]` does not intersect the query interval.
4. Decode matching blocks sequentially.
5. Return points in the requested interval through callback.

For `aggregate`:

1. Decode only blocks that intersect the interval.
2. Do not allocate a huge result array.
3. Update min/max/sum/count while scanning decoded points.
4. Return the result.

This means aggregation is streaming over decoded points.

## CSV export

CSV output format:

```csv
timestamp,value
1710000000000,72.4
1710000001000,72.5
```

Use `.` as decimal separator.
Use newline `\n`.

## Build system

Use CMake.

Expected commands:

```bash
mkdir build
cd build
cmake ..
cmake --build .
ctest
```

The build should produce:

```text
libtsedge.so
libtsedge.a optional
examples/tsedge_demo
bench/tsedge_bench
```

## Recommended project structure

```text
.
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── include
│   └── tsedge.h
├── src
│   ├── api
│   │   └── tsedge.c
│   ├── core
│   │   ├── db.c
│   │   ├── db.h
│   │   ├── series.c
│   │   ├── series.h
│   │   ├── series_index.c
│   │   ├── series_index.h
│   │   ├── series_query.c
│   │   ├── series_query.h
│   │   ├── series_stats.c
│   │   ├── series_stats.h
│   │   ├── series_retention.c
│   │   └── series_retention.h
│   ├── storage
│   │   ├── block.c
│   │   ├── block.h
│   │   ├── segment.c
│   │   ├── segment.h
│   │   ├── segment_files.c
│   │   ├── segment_files.h
│   │   ├── segment_rotation.c
│   │   ├── segment_rotation.h
│   │   ├── wal.c
│   │   └── wal.h
│   ├── compression
│   │   ├── bitstream.c
│   │   ├── bitstream.h
│   │   ├── compress.c
│   │   └── compress.h
│   └── export
│       ├── csv.c
│       └── csv.h
├── examples
│   └── tsedge_demo.c
├── tests
│   ├── test_main.c
│   ├── test_compress.c
│   ├── test_db.c
│   └── test_recovery.c
└── bench
    └── tsedge_bench.c
```

New internal modules should be placed in the appropriate layer. The public API
is changed only through `include/tsedge.h`; storage logic must not move into
`src/api`; compression must not depend on database or series modules; WAL must
not depend on CSV, query code, or the public facade. Before finishing changes,
run build, tests, demo, and benchmark.

## Testing requirements

Add tests for:

1. Open and close database.
2. Create series.
3. Append one point.
4. Append many points.
5. Read exact range.
6. Read empty range.
7. Aggregate min/max/sum/avg/count.
8. Export CSV.
9. Timestamp compression roundtrip.
10. Value compression roundtrip.
11. WAL recovery after reopening.
12. Multiple series.

Compression roundtrip tests are mandatory.

## Benchmark requirements

Create a benchmark program:

```text
bench/tsedge_bench.c
```

It should generate synthetic data and measure:

1. Write throughput, points/sec.
2. Read throughput, points/sec.
3. Aggregate time.
4. Database directory size.
5. Compression ratio compared to raw binary format.

Use at least these datasets:

1. Smooth data:

```text
value = 70.0 + sin(i * 0.001) + small_noise
```

2. Noisy data:

```text
value = random_double
```

3. Step-like data:

```text
value changes every N points
```

The benchmark output should be easy to copy into the diploma:

```text
dataset=smooth
points=1000000
write_points_per_sec=...
read_points_per_sec=...
db_size_bytes=...
raw_size_bytes=16000000
compression_ratio=...
```

## Comparison targets for diploma

The library itself only needs to implement TSEdge.

For the diploma benchmark chapter, compare TSEdge with:

1. Raw binary file:
   - int64 timestamp
   - double value
   - 16 bytes per point
2. CSV file:
   - text format
3. SQLite:
   - table with timestamp INTEGER and value REAL

It is acceptable to implement comparison benchmarks as separate programs or scripts later.

## Error handling

- Return integer status codes.
- Do not call `exit()` inside library code.
- Do not print from library code except in examples or benchmarks.
- Validate all public API arguments.
- Handle allocation failures.
- Close files on error.
- Keep error paths simple.

## Code style

- C11.
- No C++ in library.
- No external dependencies for the core library.
- Prefer small functions.
- Prefer clear names over clever names.
- Use `static` for private functions.
- Keep public API stable.
- Do not expose internal structs.
- Use `stdint.h`, `stddef.h`, `stdbool.h`.
- Avoid global mutable state.
- Avoid undefined behavior.
- Do not assume little-endian silently; if binary format uses little-endian, document it and encode/decode explicitly where practical.

## Safety and correctness priorities

Priority order:

1. Correctness.
2. Readability.
3. Stable API.
4. Persistence.
5. Compression roundtrip correctness.
6. Benchmarkability.
7. Performance.

Do not sacrifice correctness for micro-optimizations.

## Diploma-specific notes

The implementation must support the following statements in the diploma:

- TSEdge is an embedded C library.
- It is intended for Linux edge devices with filesystem support.
- It stores time-series data locally.
- It supports multiple series.
- It supports append, range read, aggregation and CSV export.
- It uses block-based storage.
- It uses lossless streaming compression.
- It uses delta-of-delta timestamp compression.
- It uses XOR-based double value compression inspired by Gorilla.
- It can be benchmarked against raw binary, CSV and SQLite.
- It does not claim to outperform Gorilla.
- It does not implement a new universal compression algorithm.
- It adapts known streaming compression methods to a small local storage engine.

## Minimal demo scenario

The example application must show the full usage flow:

```c
#include "tsedge.h"

int main(void) {
    tsedge_db* db = NULL;

    tsedge_open("./demo_db", &db);
    tsedge_create_series(db, "motor.temperature");

    for (int i = 0; i < 10000; ++i) {
        tsedge_append(db, "motor.temperature", 1710000000000LL + i * 1000, 70.0 + i * 0.001);
    }

    double avg = 0.0;
    tsedge_aggregate(
        db,
        "motor.temperature",
        1710000000000LL,
        1710000100000LL,
        TSEDGE_AGG_AVG,
        &avg
    );

    tsedge_export_csv(
        db,
        "motor.temperature",
        1710000000000LL,
        1710000100000LL,
        "temperature.csv"
    );

    tsedge_close(db);
    return 0;
}
```

## Implementation phases

Implement in this order:

### Phase 1: Skeleton

- CMake
- public header
- open/close
- create_series
- append to simple uncompressed file
- read_range
- example

### Phase 2: Tests

- test framework
- append/read tests
- aggregate tests

### Phase 3: Blocks

- in-memory buffer
- fixed-size blocks
- segment files
- block metadata

### Phase 4: Compression

- timestamp compression
- value compression
- roundtrip tests
- integrate compressed blocks

### Phase 5: WAL

- append-only WAL
- replay on open
- truncate after flush
- recovery test

### Phase 6: Benchmarks

- synthetic dataset generator
- write throughput
- read throughput
- aggregate time
- storage size
- compression ratio

Do not start with advanced optimizations.
Make each phase build and pass tests before continuing.
