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
 * Opaque handle for a loaded time series.
 *
 * Handles are owned by the database. They stay valid until the series is
 * deleted or the database is closed.
 */
typedef struct tsedge_series_handle tsedge_series_handle;

#ifndef TSEDGE_MAX_SERIES_NAME
#define TSEDGE_MAX_SERIES_NAME 255u
#endif

#ifndef TSEDGE_MAX_WINDOW_AGGREGATES
#define TSEDGE_MAX_WINDOW_AGGREGATES 1000000u
#endif

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
    TSEDGE_ERR_INTERNAL = -6,
    TSEDGE_ERR_QUOTA_EXCEEDED = -7
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
 * WAL durability policy for append operations.
 *
 * FAST buffers WAL entries in memory for maximum throughput. BALANCED flushes
 * the WAL buffer more often. STRICT flushes the WAL on every append or batch
 * and preserves the strongest crash recovery behavior.
 */
typedef enum {
    TSEDGE_DURABILITY_FAST = 0,
    TSEDGE_DURABILITY_BALANCED = 1,
    TSEDGE_DURABILITY_STRICT = 2
} tsedge_durability_mode;

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
 * Aggregate values for one non-empty half-open time window.
 *
 * Windows use [window_start, window_end) semantics: a point at window_end
 * belongs to the next window.
 */
typedef struct {
    int64_t window_start;
    int64_t window_end;
    uint64_t count;
    double min;
    double max;
    double avg;
} tsedge_window_aggregate;

/**
 * Lightweight metadata summary for one time series.
 *
 * The values are collected from the in-memory block index, the not-yet-flushed
 * buffer, and segment file sizes. Payload blocks are not decompressed.
 * Compression fields are estimates based on raw point size and actual segment
 * bytes on disk.
 */
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

/**
 * Integrity verification summary for one database directory.
 *
 * On corruption, first_error_path and first_error_message describe the first
 * problem found. The function does not repair files.
 */
typedef struct {
    size_t series_count;
    size_t segment_count;
    size_t block_count;
    size_t wal_entry_count;
    size_t error_count;
    char first_error_path[256];
    char first_error_message[256];
} tsedge_verify_report;

/**
 * One copied series entry returned by tsedge_list_series.
 *
 * The fields are derived from the loaded series registry and lightweight
 * statistics. The caller owns the array, not the strings inside the database.
 */
typedef struct {
    char name[TSEDGE_MAX_SERIES_NAME + 1u];
    uint64_t total_points;
    uint32_t segment_count;
    uint32_t block_count;
    uint64_t compressed_size_bytes;
} tsedge_series_info;

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
 * Sets the WAL durability policy for subsequent appends.
 *
 * The default is TSEDGE_DURABILITY_STRICT. Switching modes flushes any pending
 * WAL buffer first.
 */
int tsedge_set_durability(tsedge_db* db, tsedge_durability_mode mode);

/**
 * Sets a soft disk quota for database files.
 *
 * max_bytes == 0 disables the quota. When the database exceeds the limit,
 * TSEdge removes old sealed segment files. Active segments and the last segment
 * of every series are kept.
 */
int tsedge_set_disk_quota(tsedge_db* db, uint64_t max_bytes);

/**
 * Returns the current soft disk quota in bytes.
 *
 * A value of 0 means that the quota is disabled.
 */
int tsedge_get_disk_quota(tsedge_db* db, uint64_t* out_max_bytes);

/**
 * Runs disk quota cleanup immediately.
 *
 * If the database is still above the limit and no more segment files can be
 * safely removed, returns TSEDGE_ERR_QUOTA_EXCEEDED.
 */
int tsedge_enforce_disk_quota(tsedge_db* db);

/**
 * Deletes a series and removes its metadata, segments, buffers and index entries.
 *
 * Pending WAL entries are cleared through a flush before the series directory is
 * removed, so recovery cannot recreate the deleted series after reopen.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_delete_series(tsedge_db* db, const char* series_name);

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
 * Finds a series once and returns a database-owned handle for faster appends.
 *
 * The caller must not free the handle. It becomes invalid after deleting the
 * series or closing the database.
 */
int tsedge_get_series_handle(tsedge_db* db, const char* series_name, tsedge_series_handle** out_handle);

/**
 * Appends one point through a previously resolved series handle.
 *
 * This avoids repeated string lookup of the series name and uses the same
 * WAL-before-buffer write path as tsedge_append.
 */
int tsedge_append_handle(tsedge_db* db, tsedge_series_handle* handle, int64_t timestamp, double value);

/**
 * Appends multiple points through a previously resolved series handle.
 *
 * Passing count == 0 is a no-op. The handle follows the same lifetime rules as
 * tsedge_get_series_handle.
 */
int tsedge_append_batch_handle(tsedge_db* db, tsedge_series_handle* handle, const tsedge_point* points, size_t count);

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
 * Flushes buffered points of one series into a segment file.
 *
 * Calling this on an empty buffer is a no-op and returns TSEDGE_OK.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_flush(tsedge_db* db, const char* series_name);

/**
 * Flushes buffered points of all series in the database.
 *
 * Empty series buffers are ignored. After a successful flush, WAL contains only
 * data that is still not present in segment files.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_flush_all(tsedge_db* db);

/**
 * Verifies database files without modifying them.
 *
 * The check walks manifest, series metadata, segment files, block headers and
 * WAL entries. It returns TSEDGE_OK for a valid database and TSEDGE_ERR_CORRUPT
 * when a structural problem is found.
 */
int tsedge_verify(const char* db_path, tsedge_verify_report* report);

/**
 * Returns a newly allocated copy of all series known by an open database.
 *
 * Empty databases return TSEDGE_OK with *out_series == NULL and *out_count == 0.
 * The returned array must be released with tsedge_free_series_list.
 */
int tsedge_list_series(tsedge_db* db, tsedge_series_info** out_series, size_t* out_count);

/**
 * Releases a list allocated by tsedge_list_series.
 *
 * Passing NULL is safe.
 */
void tsedge_free_series_list(tsedge_series_info* series);

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
 * Deletes old data from a series at segment-file granularity.
 *
 * Only segment files whose maximum timestamp is strictly smaller than
 * older_than_timestamp are removed. Segments that partially overlap the
 * boundary are kept whole; exact deletion inside a segment would require
 * compaction and is not implemented in this prototype.
 *
 * Returns TSEDGE_OK on success or a negative error code on failure.
 */
int tsedge_delete_before(tsedge_db* db, const char* series_name, int64_t older_than_timestamp);

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
 * Computes min/max/avg/count for non-empty half-open windows.
 *
 * The query range is [start_time, end_time). Empty windows are omitted from the
 * returned array. The caller must free the array with
 * tsedge_free_window_aggregates.
 */
int tsedge_aggregate_windowed(
    tsedge_db* db,
    const char* series_name,
    int64_t start_time,
    int64_t end_time,
    int64_t window_size,
    tsedge_window_aggregate** out_windows,
    size_t* out_count
);

/**
 * Releases window aggregates allocated by tsedge_aggregate_windowed.
 *
 * Passing NULL is safe.
 */
void tsedge_free_window_aggregates(tsedge_window_aggregate* windows);

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
