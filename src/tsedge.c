#include "tsedge.h"
#include "csv.h"
#include "db.h"
#include "series.h"

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

int tsedge_append(tsedge_db* db, const char* series_name, int64_t timestamp, double value) {
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
    return tsedge_series_append(db, series, timestamp, value);
}

int tsedge_append_batch(tsedge_db* db, const char* series_name, const tsedge_point* points, size_t count) {
    if (!db || (!points && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return TSEDGE_OK;
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
    return tsedge_series_read_range(series, from_timestamp, to_timestamp, callback, user_data);
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
    return tsedge_series_aggregate(series, from_timestamp, to_timestamp, type, out_result);
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
        default:
            return "unknown error";
    }
}
