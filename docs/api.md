# Public API

The public API is declared in `include/tsedge.h`. Internal structures are not
exposed; `tsedge_db` is opaque.

All functions return `TSEDGE_OK` on success or a negative `TSEDGE_ERR_*` status
on failure.

## `tsedge_open`

Opens or creates a database directory.

Parameters:

- `path`: filesystem path to the database directory.
- `out_db`: receives the opened database handle.

Returns:

- `TSEDGE_OK` on success.
- `TSEDGE_ERR_INVALID_ARGUMENT`, `TSEDGE_ERR_IO`, `TSEDGE_ERR_NO_MEMORY`, or
  `TSEDGE_ERR_CORRUPT` on failure.

Example:

```c
tsedge_db* db = NULL;
int rc = tsedge_open("./demo_db", &db);
```

## `tsedge_close`

Flushes pending data and closes a database handle.

Parameters:

- `db`: database handle, or `NULL`.

Returns:

- `TSEDGE_OK` on success.
- An error status if flushing pending data fails.

Example:

```c
tsedge_close(db);
```

## `tsedge_create_series`

Creates a named time series in the database. If the series already exists, the
operation succeeds without creating a duplicate.

Parameters:

- `db`: database handle.
- `name`: series name, such as `motor.temperature`.

Returns:

- `TSEDGE_OK` on success.
- `TSEDGE_ERR_INVALID_ARGUMENT`, `TSEDGE_ERR_IO`, or `TSEDGE_ERR_NO_MEMORY` on
  failure.

Example:

```c
tsedge_create_series(db, "motor.temperature");
```

## `tsedge_append`

Appends one point to an existing series. A point contains an `int64_t`
timestamp and a `double` value.

Parameters:

- `db`: database handle.
- `series_name`: existing series name.
- `timestamp`: point timestamp.
- `value`: point value.

Returns:

- `TSEDGE_OK` on success.
- `TSEDGE_ERR_NOT_FOUND` if the series does not exist.
- Other error statuses for invalid arguments, I/O failures, or allocation
  failures.

Example:

```c
tsedge_append(db, "motor.temperature", 1710000000000LL, 72.4);
```

## `tsedge_append_batch`

Appends multiple points to an existing series. The public argument validation
and series lookup are performed once for the whole batch, while each point still
uses the same WAL-before-buffer append path as `tsedge_append`.

Parameters:

- `db`: database handle.
- `series_name`: existing series name.
- `points`: array of points to append.
- `count`: number of points in the array.

Returns:

- `TSEDGE_OK` on success.
- `TSEDGE_OK` when `count` is `0`; in this case `points` may be `NULL`.
- `TSEDGE_ERR_NOT_FOUND` if the series does not exist.
- Other error statuses for invalid arguments, I/O failures, or allocation
  failures.

If an error happens after some points have already been written, those earlier
points may remain accepted and visible after recovery. The function does not
provide all-or-nothing batch transactions.

Example:

```c
tsedge_point points[3] = {
    {1710000000000LL, 72.4},
    {1710000001000LL, 72.5},
    {1710000002000LL, 72.6},
};

tsedge_append_batch(db, "motor.temperature", points, 3);
```

## `tsedge_flush`

Flushes buffered points of one series into a segment file. If the buffer is
empty, the call succeeds and does nothing.

The function uses the same internal block/segment write path as automatic
flushes. After success, the block index and WAL are updated.

Parameters:

- `db`: database handle.
- `series_name`: existing series name.

Returns:

- `TSEDGE_OK` on success, including an empty buffer.
- `TSEDGE_ERR_NOT_FOUND` if the series does not exist.
- Other error statuses for invalid arguments, rotation failures, I/O failures or
  allocation failures.

Example:

```c
tsedge_append(db, "motor.temperature", timestamp, value);
tsedge_flush(db, "motor.temperature");
tsedge_export_csv(db, "motor.temperature", from, to, "temperature.csv");
```

## `tsedge_flush_all`

Flushes buffered points of all series in the database. Empty buffers are
ignored. If one series fails to flush, the function returns that error.

After a successful call, WAL contains only data that is still not present in
segment files.

Parameters:

- `db`: database handle.

Returns:

- `TSEDGE_OK` on success.
- Other error statuses for invalid arguments, rotation failures, I/O failures or
  allocation failures.

Example:

```c
tsedge_flush_all(db);
```

## `tsedge_verify`

Checks the structure of a database directory without modifying files. The
function verifies the root directory, `manifest.txt`, series metadata,
`segment_*.tse` files, block headers, payload boundaries and WAL entries.

The output structure is:

```c
typedef struct {
    size_t series_count;
    size_t segment_count;
    size_t block_count;
    size_t wal_entry_count;
    size_t error_count;
    char first_error_path[256];
    char first_error_message[256];
} tsedge_verify_report;
```

Parameters:

- `db_path`: filesystem path to an existing TSEdge database directory.
- `report`: receives counters and the first error, if any.

Returns:

- `TSEDGE_OK` when the database is structurally valid.
- `TSEDGE_ERR_CORRUPT` when a corrupt file or invalid layout is found.
- Other error statuses for invalid arguments, I/O failures or allocation
  failures.

Example:

```c
tsedge_verify_report report;
int rc = tsedge_verify("demo_db", &report);
if (rc == TSEDGE_OK) {
    printf("database is valid\n");
} else {
    printf("database is corrupted: %s\n", report.first_error_message);
}
```

## `tsedge_get_series_stats`

Returns lightweight statistics for an existing series. The function uses the
in-memory block index, the current unflushed buffer and segment file sizes. It
does not decompress block payloads or scan all stored points.

The output structure is:

```c
typedef struct {
    size_t block_count;
    size_t buffered_points;
    size_t total_indexed_points;
    size_t segment_count;
    int has_time_range;
    int64_t min_timestamp;
    int64_t max_timestamp;
    uint32_t active_segment_id;
    uint64_t segment_size_bytes;
    uint64_t total_segment_size_bytes;
    uint64_t raw_size_estimate_bytes;
    uint64_t compressed_size_bytes;
    double compression_ratio;
    double bytes_per_point;
} tsedge_series_stats;
```

Fields:

- `block_count`: number of blocks currently present in the in-memory block
  index.
- `buffered_points`: number of points still kept in the memory buffer.
- `total_indexed_points`: sum of `point_count` across indexed blocks.
- `segment_count`: number of existing `segment_*.tse` files for the series.
- `has_time_range`: `1` when the series has at least one point in blocks or
  buffer, otherwise `0`.
- `min_timestamp` and `max_timestamp`: timestamp range across indexed blocks and
  buffered points when `has_time_range` is `1`.
- `active_segment_id`: id used for the next block flush.
- `segment_size_bytes`: total size of all segment files. This keeps older
  callers useful after segment rotation.
- `total_segment_size_bytes`: explicit total size of all segment files.
- `raw_size_estimate_bytes`: estimated uncompressed size as
  `point_count * sizeof(tsedge_point)`.
- `compressed_size_bytes`: actual bytes occupied by the series segment files.
- `compression_ratio`: `raw_size_estimate_bytes / compressed_size_bytes`, or
  `0.0` when there are no segment bytes yet.
- `bytes_per_point`: average segment bytes per point, or `0.0` for an empty
  series.

Parameters:

- `db`: database handle.
- `series_name`: existing series name.
- `out_stats`: receives the statistics.

Returns:

- `TSEDGE_OK` on success.
- `TSEDGE_ERR_NOT_FOUND` if the series does not exist.
- Other error statuses for invalid arguments.

Example:

```c
tsedge_series_stats stats;
int rc = tsedge_get_series_stats(db, "motor.temperature", &stats);
if (rc == TSEDGE_OK && stats.has_time_range) {
    printf("segments=%zu active=%u blocks=%zu buffered=%zu range=%lld..%lld\n",
           stats.segment_count,
           stats.active_segment_id,
           stats.block_count,
           stats.buffered_points,
           (long long)stats.min_timestamp,
           (long long)stats.max_timestamp);
}
```

## `tsedge_delete_before`

Deletes old data from a series at segment-file granularity. A segment is removed
only when every block in that file is older than `older_than_timestamp`, meaning
the segment maximum timestamp is less than the threshold. Segments that
partially overlap the threshold are kept unchanged.

Before deletion, TSEdge flushes the current in-memory buffer. This keeps the WAL
consistent with the segment files and makes the retention decision based on
durable block metadata. After deleting files, the in-memory block index is
rebuilt from the remaining segment files.

Parameters:

- `db`: database handle.
- `series_name`: existing series name.
- `older_than_timestamp`: delete segments whose maximum timestamp is strictly
  less than this value.

Returns:

- `TSEDGE_OK` on success, including when no segment matches the threshold.
- `TSEDGE_ERR_NOT_FOUND` if the series does not exist.
- Other error statuses for invalid arguments, flush failures, I/O failures or
  corrupt segment data.

Example:

```c
int rc = tsedge_delete_before(db, "motor.temperature", 1710001000000LL);
if (rc != TSEDGE_OK) {
    fprintf(stderr, "retention failed: %s\n", tsedge_strerror(rc));
}
```

This function does not rewrite partially old segments. Exact deletion inside a
segment requires compaction, which is not implemented in the current prototype.

## `tsedge_read_range`

Reads points from a series over an inclusive timestamp range and passes each
matching point to a callback.

Parameters:

- `db`: database handle.
- `series_name`: existing series name.
- `from_timestamp`: inclusive range start.
- `to_timestamp`: inclusive range end.
- `callback`: function called for each matching point.
- `user_data`: pointer passed through to the callback.

If the callback returns a non-zero value, the range scan stops early and
`tsedge_read_range` still returns `TSEDGE_OK`. This is intended for callers that
only need the first matching point or want to cancel a scan.

Returns:

- `TSEDGE_OK` on success.
- `TSEDGE_ERR_NOT_FOUND` if the series does not exist.
- Other error statuses for invalid arguments, I/O failures, or corrupt data.

Example:

```c
static int print_point(const tsedge_point* point, void* user_data) {
    (void)user_data;
    printf("%lld %.17g\n", (long long)point->timestamp, point->value);
    return 0;
}

tsedge_read_range(db, "motor.temperature", 1710000000000LL,
                  1710000100000LL, print_point, NULL);
```

## `tsedge_aggregate`

Computes a streaming aggregate over an inclusive timestamp range.

Parameters:

- `db`: database handle.
- `series_name`: existing series name.
- `from_timestamp`: inclusive range start.
- `to_timestamp`: inclusive range end.
- `type`: one of `TSEDGE_AGG_MIN`, `TSEDGE_AGG_MAX`, `TSEDGE_AGG_SUM`,
  `TSEDGE_AGG_AVG`, or `TSEDGE_AGG_COUNT`.
- `out_result`: receives the aggregate value.

Returns:

- `TSEDGE_OK` on success.
- `TSEDGE_ERR_NOT_FOUND` if the series or requested non-count aggregate data is
  not found.
- Other error statuses for invalid arguments, I/O failures, or corrupt data.

Example:

```c
double avg = 0.0;
tsedge_aggregate(db, "motor.temperature", 1710000000000LL,
                 1710000100000LL, TSEDGE_AGG_AVG, &avg);
```

## `tsedge_export_csv`

Exports points from an inclusive timestamp range to a CSV file.

Parameters:

- `db`: database handle.
- `series_name`: existing series name.
- `from_timestamp`: inclusive range start.
- `to_timestamp`: inclusive range end.
- `output_path`: path to the output CSV file.

Returns:

- `TSEDGE_OK` on success.
- `TSEDGE_ERR_NOT_FOUND` if the series does not exist.
- Other error statuses for invalid arguments, I/O failures, or corrupt data.

CSV format:

```csv
timestamp,value
1710000000000,72.4
```

Example:

```c
tsedge_export_csv(db, "motor.temperature", 1710000000000LL,
                  1710000100000LL, "temperature.csv");
```

## `tsedge_strerror`

Returns a static human-readable string for a TSEdge status code.

Parameters:

- `status`: status code returned by a TSEdge API function.

Returns:

- A static string. The caller must not free it.

Example:

```c
int rc = tsedge_create_series(db, "motor.temperature");
if (rc != TSEDGE_OK) {
    fprintf(stderr, "%s\n", tsedge_strerror(rc));
}
```
