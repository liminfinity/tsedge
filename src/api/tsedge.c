#include "tsedge.h"
#include "csv.h"
#include "db.h"
#include "db_quota.h"
#include "series.h"
#include "series_delete.h"
#include "series_list.h"
#include "series_query.h"
#include "series_retention.h"
#include "series_stats.h"
#include "verify.h"
#include "wal.h"

#include <stdlib.h>

int tsedge_open(const char* path, tsedge_db** out_db) {
    if (!path || !out_db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    return tsedge_db_open_internal(path, out_db);
}

int tsedge_close(tsedge_db* db) {
    return tsedge_db_close_internal(db);
}

int tsedge_create_series(tsedge_db* db, const char* name) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return tsedge_db_create_series_internal(db, name);
}

int tsedge_set_durability(tsedge_db* db, tsedge_durability_mode mode) {
    if (!db ||
        (mode != TSEDGE_DURABILITY_FAST &&
         mode != TSEDGE_DURABILITY_BALANCED &&
         mode != TSEDGE_DURABILITY_STRICT)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_wal_flush(db);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    db->durability = mode;
    return TSEDGE_OK;
}

int tsedge_set_disk_quota(tsedge_db* db, uint64_t max_bytes) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    db->disk_quota_bytes = max_bytes;
    db->disk_quota_exceeded = 0;
    return TSEDGE_OK;
}

int tsedge_get_disk_quota(tsedge_db* db, uint64_t* out_max_bytes) {
    if (!db || !out_max_bytes) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    *out_max_bytes = db->disk_quota_bytes;
    return TSEDGE_OK;
}

int tsedge_enforce_disk_quota(tsedge_db* db) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_wal_flush(db);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return tsedge_db_enforce_disk_quota(db);
}

int tsedge_delete_series(tsedge_db* db, const char* series_name) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return tsedge_db_delete_series(db, series_name);
}

int tsedge_append(tsedge_db* db, const char* series_name, int64_t timestamp, double value) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (db->disk_quota_exceeded) {
        return TSEDGE_ERR_QUOTA_EXCEEDED;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, series_name);
    if (!series) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_series_append(db, series, timestamp, value);
}

int tsedge_get_series_handle(tsedge_db* db, const char* series_name, tsedge_series_handle** out_handle) {
    if (!db || !out_handle) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return tsedge_db_get_series_handle(db, series_name, out_handle);
}

int tsedge_append_handle(tsedge_db* db, tsedge_series_handle* handle, int64_t timestamp, double value) {
    if (db && db->disk_quota_exceeded) {
        return TSEDGE_ERR_QUOTA_EXCEEDED;
    }
    tsedge_series* series = NULL;
    int rc = tsedge_db_resolve_series_handle(db, handle, &series);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return tsedge_series_append(db, series, timestamp, value);
}

int tsedge_append_batch(tsedge_db* db, const char* series_name, const tsedge_point* points, size_t count) {
    if (!db || (!points && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return TSEDGE_OK;
    }
    if (db->disk_quota_exceeded) {
        return TSEDGE_ERR_QUOTA_EXCEEDED;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, series_name);
    if (!series) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_series_append_batch(db, series, points, count);
}

int tsedge_append_batch_handle(tsedge_db* db, tsedge_series_handle* handle, const tsedge_point* points, size_t count) {
    if (!db || !handle || (!points && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return TSEDGE_OK;
    }
    if (db->disk_quota_exceeded) {
        return TSEDGE_ERR_QUOTA_EXCEEDED;
    }
    tsedge_series* series = NULL;
    int rc = tsedge_db_resolve_series_handle(db, handle, &series);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return tsedge_series_append_batch(db, series, points, count);
}

int tsedge_flush(tsedge_db* db, const char* series_name) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, series_name);
    if (!series) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_series_flush(db, series, true);
}

int tsedge_flush_all(tsedge_db* db) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    return tsedge_db_flush_all(db);
}

int tsedge_verify(const char* db_path, tsedge_verify_report* report) {
    if (!db_path || !report) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    return tsedge_verify_internal(db_path, report);
}

int tsedge_list_series(tsedge_db* db, tsedge_series_info** out_series, size_t* out_count) {
    return tsedge_db_list_series(db, out_series, out_count);
}

void tsedge_free_series_list(tsedge_series_info* series) {
    free(series);
}

int tsedge_get_series_stats(tsedge_db* db, const char* series_name, tsedge_series_stats* out_stats) {
    if (!db || !out_stats) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, series_name);
    if (!series) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_series_get_stats(series, out_stats);
}

int tsedge_delete_before(tsedge_db* db, const char* series_name, int64_t older_than_timestamp) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, series_name);
    if (!series) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_series_delete_before(db, series, older_than_timestamp);
}

int tsedge_read_range(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data
) {
    if (!db || !callback || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, series_name);
    if (!series) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_series_read_range(db, series, from_timestamp, to_timestamp, callback, user_data);
}

int tsedge_aggregate(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_agg_type type,
    double* out_result
) {
    if (!db || !out_result || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, series_name);
    if (!series) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_series_aggregate(db, series, from_timestamp, to_timestamp, type, out_result);
}

int tsedge_aggregate_windowed(
    tsedge_db* db,
    const char* series_name,
    int64_t start_time,
    int64_t end_time,
    int64_t window_size,
    tsedge_window_aggregate** out_windows,
    size_t* out_count
) {
    if (!db || !out_windows || !out_count || window_size <= 0 || start_time > end_time) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, series_name);
    if (!series) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_series_aggregate_windowed(db, series, start_time, end_time, window_size, out_windows, out_count);
}

void tsedge_free_window_aggregates(tsedge_window_aggregate* windows) {
    free(windows);
}

int tsedge_export_csv(
    tsedge_db* db,
    const char* series_name,
    int64_t from_timestamp,
    int64_t to_timestamp,
    const char* output_path
) {
    if (!db || !output_path || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (!tsedge_db_find_series(db, series_name)) {
        return TSEDGE_ERR_NOT_FOUND;
    }
    return tsedge_csv_export(db, series_name, from_timestamp, to_timestamp, output_path);
}

const char* tsedge_strerror(int status) {
    switch (status) {
        case TSEDGE_OK:
            return "ok";
        case TSEDGE_ERR_INVALID_ARGUMENT:
            return "invalid argument";
        case TSEDGE_ERR_IO:
            return "io error";
        case TSEDGE_ERR_NO_MEMORY:
            return "out of memory";
        case TSEDGE_ERR_NOT_FOUND:
            return "not found";
        case TSEDGE_ERR_CORRUPT:
            return "corrupt data";
        case TSEDGE_ERR_INTERNAL:
            return "internal error";
        case TSEDGE_ERR_QUOTA_EXCEEDED:
            return "disk quota exceeded";
        default:
            return "unknown error";
    }
}
