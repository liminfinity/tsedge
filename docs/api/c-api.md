# C API

The public C API is declared in `include/tsedge.h`. This page gives the shape of the API and the common call flow. For exact signatures, use the header as the source of truth.

## Opening and closing

- `tsedge_open`
- `tsedge_close`

`tsedge_open` opens or creates a database directory. `tsedge_close` flushes pending data and releases the database handle.

Passing `NULL` to `tsedge_close` is safe, which makes cleanup paths simpler.

## Series management

- `tsedge_create_series`
- `tsedge_delete_series`
- `tsedge_list_series`
- `tsedge_free_series_list`
- `tsedge_get_series_handle`

Series handles are database-owned and can speed up repeated writes to the same series.

Use names for simple code and handles for hot write paths. A handle stays valid until the series is deleted or the database is closed.

## Writing points

- `tsedge_append`
- `tsedge_append_batch`
- `tsedge_append_handle`
- `tsedge_append_batch_handle`
- `tsedge_flush`
- `tsedge_flush_all`

Batch writes avoid repeating public validation and series lookup for every point. They are faster than calling `tsedge_append` in a loop, especially when combined with a series handle.

If a batch fails partway through, accepted points before the error may remain stored. TSEdge does not provide all-or-nothing batch transactions.

## Reading and aggregates

- `tsedge_read_range`
- `tsedge_aggregate`
- `tsedge_aggregate_windowed`
- `tsedge_free_window_aggregates`

Range reads stream points through a callback. Aggregates are computed while scanning candidate blocks. Windowed aggregates return compact non-empty time windows.

Use `tsedge_read_range` when the application needs raw points. Use `tsedge_aggregate` or `tsedge_aggregate_windowed` when a summary is enough.

## Quota, retention and verification

- `tsedge_set_disk_quota`
- `tsedge_get_disk_quota`
- `tsedge_enforce_disk_quota`
- `tsedge_delete_before`
- `tsedge_verify`
- `tsedge_get_series_stats`

Retention and quota cleanup work at segment-file granularity. Verification inspects database files without repairing them.

`tsedge_verify` is useful in tests, diagnostics and maintenance tools. It reports the first structural problem found.

## CSV export and errors

- `tsedge_export_csv`
- `tsedge_strerror`

API calls return integer status codes. `TSEDGE_OK` means success; negative values indicate validation, I/O, memory, not-found, corruption, quota or internal errors.

Library code does not call `exit()` and does not print diagnostics. Applications decide how to report or recover from errors.

## Minimal example

```c
#include <tsedge.h>
#include <stdio.h>

int main(void) {
    tsedge_db* db = NULL;

    if (tsedge_open("demo_db", &db) != TSEDGE_OK) {
        return 1;
    }

    tsedge_create_series(db, "air.temperature");
    tsedge_append(db, "air.temperature", 1, 10.0);
    tsedge_append(db, "air.temperature", 2, 20.0);

    double avg = 0.0;
    tsedge_aggregate(
        db,
        "air.temperature",
        1,
        3,
        TSEDGE_AGG_AVG,
        &avg
    );

    printf("avg = %.3f\n", avg);

    tsedge_close(db);
    return 0;
}
```
