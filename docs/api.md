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
