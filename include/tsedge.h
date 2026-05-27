#ifndef TSEDGE_H
#define TSEDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque database handle.
 *
 * TSEdge is an embedded library, so callers keep this handle locally and pass it
 * directly to API functions. The internal storage layout is intentionally hidden
 * from applications.
 */
typedef struct tsedge_db tsedge_db;

/**
 * Status codes returned by the public API.
 *
 * TSEDGE_OK means success. Negative values describe validation, I/O, memory,
 * corruption, or internal failures without printing or exiting from library code.
 */
typedef enum {
    TSEDGE_OK = 0,
    TSEDGE_ERR_INVALID_ARGUMENT = -1,
    TSEDGE_ERR_IO = -2,
    TSEDGE_ERR_NO_MEMORY = -3,
    TSEDGE_ERR_NOT_FOUND = -4,
    TSEDGE_ERR_CORRUPT = -5,
    TSEDGE_ERR_INTERNAL = -6
} tsedge_status;

/**
 * Supported streaming aggregate operations over a timestamp range.
 */
typedef enum {
    TSEDGE_AGG_MIN,
    TSEDGE_AGG_MAX,
    TSEDGE_AGG_SUM,
    TSEDGE_AGG_AVG,
    TSEDGE_AGG_COUNT
} tsedge_agg_type;

/**
 * One time-series data point.
 *
 * The first prototype stores exactly one int64 timestamp and one double value
 * per point.
 */
typedef struct {
    int64_t timestamp;
    double value;
} tsedge_point;

/**
 * Lightweight metadata summary for one time series.
 *
 * The values are collected from the in-memory block index, the not-yet-flushed
 * buffer, and the segment file size. Payload blocks are not decompressed.
 */
typedef struct {
    size_t block_count;
    size_t buffered_points;
    size_t total_indexed_points;
    int has_time_range;
    int64_t min_timestamp;
    int64_t max_timestamp;
    uint64_t segment_size_bytes;
} tsedge_series_stats;

/**
 * Callback used by range reads.
 *
 * Returning a non-zero value stops the scan early. The callback receives a
 * pointer valid only for the duration of the call.
 */
typedef int (*tsedge_point_callback)(const tsedge_point* point, void* user_data);

/**
 * Opens or creates a TSEdge database at the given filesystem path.
 *
 * The function creates the database directory if it does not exist and
 * initializes internal metadata, series state and recovery structures.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_open(const char* path, tsedge_db** out_db);

/**
 * Flushes pending in-memory data and closes a database handle.
 *
 * Passing NULL is accepted and treated as a no-op.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_close(tsedge_db* db);

/**
 * Creates a named time series inside the database.
 *
 * If the series already exists, the operation succeeds without creating a
 * duplicate.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_create_series(tsedge_db* db, const char* name);

/**
 * Appends a single point to an existing series.
 *
 * The point is first written to the WAL and then placed into the in-memory
 * buffer so it can be recovered after a crash.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_append(tsedge_db* db, const char* series_name, int64_t timestamp, double value);

/**
 * Appends multiple points to an existing series.
 *
 * The series name is validated and resolved once, then each point follows the
 * same WAL-before-buffer path as tsedge_append. If an error happens in the
 * middle of the batch, points accepted before the error may remain stored.
 *
 * Passing count == 0 is a no-op and returns TSEDGE_OK.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_append_batch(tsedge_db* db, const char* series_name, const tsedge_point* points, size_t count);

/**
 * Returns lightweight statistics for an existing series.
 *
 * The function uses block metadata and the current memory buffer, so it can
 * report series state without scanning or decompressing all points.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_get_series_stats(tsedge_db* db, const char* series_name, tsedge_series_stats* out_stats);

/**
 * Reads points from an inclusive timestamp range.
 *
 * Matching points are streamed through the callback instead of being returned
 * as a large allocated array.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_read_range(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data
);

/**
 * Computes a basic aggregate over an inclusive timestamp range.
 *
 * Aggregation is performed while scanning points, so the implementation does
 * not need to load the entire range into memory.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_aggregate(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_agg_type type,
    double* out_result
);

/**
 * Exports points from an inclusive timestamp range to a CSV file.
 *
 * The CSV output is an external interchange format with a header line:
 * timestamp,value.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_export_csv(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    const char* output_path
);

/**
 * Returns a static human-readable message for a TSEdge status code.
 *
 * The returned string must not be freed by the caller.
 */
const char* tsedge_strerror(int status);

#ifdef __cplusplus
}
#endif

#endif
