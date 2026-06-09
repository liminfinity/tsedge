#include "db.h"
#include "tsedge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int tsedge_test_failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++tsedge_test_failures; \
        return; \
    } \
} while (0)

#define CHECK_OK(expr) do { \
    int _rc = (expr); \
    if (_rc != TSEDGE_OK) { \
        fprintf(stderr, "CHECK_OK failed at %s:%d: %s -> %s\n", __FILE__, __LINE__, #expr, tsedge_strerror(_rc)); \
        ++tsedge_test_failures; \
        return; \
    } \
} while (0)

typedef struct {
    tsedge_point points[8192];
    size_t count;
} point_vec;

static int collect_cb(const tsedge_point* point, void* user_data) {
    point_vec* vec = (point_vec*)user_data;
    if (vec->count < sizeof(vec->points) / sizeof(vec->points[0])) {
        vec->points[vec->count++] = *point;
    }
    return 0;
}

static int stop_after_first_cb(const tsedge_point* point, void* user_data) {
    (void)point;
    size_t* count = (size_t*)user_data;
    ++(*count);
    return 1;
}

static int count_cb(const tsedge_point* point, void* user_data) {
    (void)point;
    size_t* count = (size_t*)user_data;
    ++(*count);
    return 0;
}

static size_t count_csv_data_rows(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    char buf[256];
    size_t rows = 0;
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0;
    }
    while (fgets(buf, sizeof(buf), f)) {
        ++rows;
    }
    fclose(f);
    return rows;
}

static void rm_rf(const char* path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    int rc = system(cmd);
    (void)rc;
}

static const char* temp_path(const char* name) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/tsedge_%s_%ld", name, (long)getpid());
    rm_rf(path);
    return path;
}

static int path_exists(const char* path) {
    return access(path, F_OK) == 0;
}

static void make_series_file_path(char* out, size_t out_size, const char* db_path, const char* series_name, const char* filename) {
    snprintf(out, out_size, "%s/series/%s/%s", db_path, series_name, filename);
}

static tsedge_point* make_linear_points(size_t count, int64_t timestamp_base) {
    tsedge_point* points = (tsedge_point*)malloc(count * sizeof(*points));
    if (!points) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        points[i].timestamp = timestamp_base + (int64_t)i;
        points[i].value = (double)i;
    }
    return points;
}

void test_open_close(void) {
    const char* path = temp_path("open_close");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK(db != NULL);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_append_read_aggregate_csv(void) {
    const char* path = temp_path("db");
    char csv_path[512];
    snprintf(csv_path, sizeof(csv_path), "%s/out.csv", path);
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "motor.temperature"));
    CHECK_OK(tsedge_append(db, "motor.temperature", 1000, 10.0));
    for (int i = 1; i < 100; ++i) {
        CHECK_OK(tsedge_append(db, "motor.temperature", 1000 + i * 10, (double)i));
    }

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    /* Range reads are inclusive and filter by timestamp. */
    CHECK_OK(tsedge_read_range(db, "motor.temperature", 1000, 1030, collect_cb, &vec));
    CHECK(vec.count == 4);
    CHECK(vec.points[0].timestamp == 1000);
    CHECK(vec.points[3].timestamp == 1030);

    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "motor.temperature", 5000, 6000, collect_cb, &vec));
    CHECK(vec.count == 0);

    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "motor.temperature", 1010, 1040, TSEDGE_AGG_MIN, &result));
    CHECK(result == 1.0);
    CHECK_OK(tsedge_aggregate(db, "motor.temperature", 1010, 1040, TSEDGE_AGG_MAX, &result));
    CHECK(result == 4.0);
    CHECK_OK(tsedge_aggregate(db, "motor.temperature", 1010, 1040, TSEDGE_AGG_SUM, &result));
    CHECK(result == 10.0);
    CHECK_OK(tsedge_aggregate(db, "motor.temperature", 1010, 1040, TSEDGE_AGG_AVG, &result));
    CHECK(result == 2.5);
    CHECK_OK(tsedge_aggregate(db, "motor.temperature", 1010, 1040, TSEDGE_AGG_COUNT, &result));
    CHECK(result == 4.0);

    CHECK_OK(tsedge_export_csv(db, "motor.temperature", 1000, 1010, csv_path));
    FILE* f = fopen(csv_path, "r");
    CHECK(f != NULL);
    char buf[128];
    CHECK(fgets(buf, sizeof(buf), f) != NULL);
    CHECK(strcmp(buf, "timestamp,value\n") == 0);
    CHECK(fgets(buf, sizeof(buf), f) != NULL);
    fclose(f);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_many_points_blocks_and_reopen(void) {
    const char* path = temp_path("blocks");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 5000; ++i) {
        CHECK_OK(tsedge_append(db, "s", i, 1.0 + (double)i));
    }
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    /* This range crosses the first flushed block boundary. */
    CHECK_OK(tsedge_read_range(db, "s", 4090, 4100, collect_cb, &vec));
    CHECK(vec.count == 11);
    CHECK(vec.points[0].timestamp == 4090);
    CHECK(vec.points[10].timestamp == 4100);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_append_batch_zero_one_many(void) {
    const char* path = temp_path("batch_basic");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", NULL, 0));

    tsedge_point one = {10, 1.5};
    CHECK_OK(tsedge_append_batch(db, "s", &one, 1));

    tsedge_point points[4] = {
        {20, 2.0},
        {30, 3.0},
        {40, 4.0},
        {50, 5.0},
    };
    CHECK_OK(tsedge_append_batch(db, "s", points, 4));

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 0, 100, collect_cb, &vec));
    CHECK(vec.count == 5);
    CHECK(vec.points[0].timestamp == 10);
    CHECK(vec.points[4].timestamp == 50);

    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "s", 0, 100, TSEDGE_AGG_SUM, &result));
    CHECK(result == 15.5);
    CHECK_OK(tsedge_aggregate(db, "s", 0, 100, TSEDGE_AGG_COUNT, &result));
    CHECK(result == 5.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_append_batch_multiple_blocks(void) {
    const char* path = temp_path("batch_blocks");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u + 123u;
    tsedge_point* points = (tsedge_point*)malloc(count * sizeof(*points));
    CHECK(points != NULL);
    for (size_t i = 0; i < count; ++i) {
        points[i].timestamp = (int64_t)i;
        points[i].value = (double)i;
    }

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 0, (int64_t)count - 1, count_cb, &read_count));
    CHECK(read_count == count);

    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "s", 0, (int64_t)count - 1, TSEDGE_AGG_SUM, &result));
    CHECK(result == ((double)(count - 1u) * (double)count) / 2.0);
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series* series = tsedge_db_find_series(db, "s");
    CHECK(series != NULL);
    CHECK(series->block_index_count == 3);
    read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 4090, 4105, count_cb, &read_count));
    CHECK(read_count == 16);
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_flush_single_series(void) {
    const char* path = temp_path("flush_single");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));

    CHECK_OK(tsedge_append(db, "s", 100, 1.0));
    CHECK_OK(tsedge_append(db, "s", 200, 2.0));
    CHECK_OK(tsedge_append(db, "s", 300, 3.0));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.buffered_points == 3);
    CHECK(stats.block_count == 0);

    CHECK_OK(tsedge_flush(db, "s"));
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.buffered_points == 0);
    CHECK(stats.block_count > 0);
    CHECK(stats.total_indexed_points == 3);
    CHECK(stats.segment_count == 1);

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 0, 400, collect_cb, &vec));
    CHECK(vec.count == 3);
    CHECK(vec.points[0].timestamp == 100);
    CHECK(vec.points[2].timestamp == 300);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_flush_empty_buffer(void) {
    const char* path = temp_path("flush_empty");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_flush(db, "s"));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.buffered_points == 0);
    CHECK(stats.block_count == 0);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_flush_all(void) {
    const char* path = temp_path("flush_all");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "a"));
    CHECK_OK(tsedge_create_series(db, "b"));

    CHECK_OK(tsedge_append(db, "a", 1, 10.0));
    CHECK_OK(tsedge_append(db, "a", 2, 20.0));
    CHECK_OK(tsedge_append(db, "b", 1, 30.0));
    CHECK_OK(tsedge_append(db, "b", 2, 40.0));
    CHECK_OK(tsedge_flush_all(db));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "a", &stats));
    CHECK(stats.buffered_points == 0);
    CHECK(stats.total_indexed_points == 2);
    CHECK_OK(tsedge_get_series_stats(db, "b", &stats));
    CHECK(stats.buffered_points == 0);
    CHECK(stats.total_indexed_points == 2);

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "a", 0, 10, count_cb, &read_count));
    CHECK(read_count == 2);
    read_count = 0;
    CHECK_OK(tsedge_read_range(db, "b", 0, 10, count_cb, &read_count));
    CHECK(read_count == 2);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_flush_invalid_args(void) {
    const char* path = temp_path("flush_invalid");
    tsedge_db* db = NULL;
    CHECK(tsedge_flush(NULL, "x") == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_flush_all(NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK_OK(tsedge_open(path, &db));
    CHECK(tsedge_flush(db, NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_flush(db, "") == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_flush(db, "missing") == TSEDGE_ERR_NOT_FOUND);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_flush_before_export_csv(void) {
    const char* path = temp_path("flush_export");
    char csv_path[512];
    snprintf(csv_path, sizeof(csv_path), "%s/out.csv", path);
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));

    CHECK_OK(tsedge_append(db, "s", 10, 1.0));
    CHECK_OK(tsedge_append(db, "s", 20, 2.0));
    CHECK_OK(tsedge_append(db, "s", 30, 3.0));
    CHECK_OK(tsedge_flush(db, "s"));
    CHECK_OK(tsedge_export_csv(db, "s", 0, 100, csv_path));
    CHECK(count_csv_data_rows(csv_path) == 3);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_valid_database(void) {
    const char* path = temp_path("verify_valid");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 16; ++i) {
        CHECK_OK(tsedge_append(db, "s", 1000 + i, (double)i));
    }
    CHECK_OK(tsedge_flush_all(db));

    tsedge_verify_report report;
    CHECK_OK(tsedge_verify(path, &report));
    CHECK(report.error_count == 0);
    CHECK(report.series_count >= 1);
    CHECK(report.segment_count >= 1);
    CHECK(report.block_count >= 1);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_empty_database(void) {
    const char* path = temp_path("verify_empty");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));

    tsedge_verify_report report;
    CHECK_OK(tsedge_verify(path, &report));
    CHECK(report.error_count == 0);
    CHECK(report.series_count == 0);
    CHECK(report.segment_count == 0);
    CHECK(report.block_count == 0);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_missing_metadata(void) {
    const char* path = temp_path("verify_missing_metadata");
    char metadata_path[512];
    make_series_file_path(metadata_path, sizeof(metadata_path), path, "s", "metadata.txt");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK(unlink(metadata_path) == 0);

    tsedge_verify_report report;
    CHECK(tsedge_verify(path, &report) == TSEDGE_ERR_CORRUPT);
    CHECK(report.error_count > 0);
    CHECK(strstr(report.first_error_path, "metadata.txt") != NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_invalid_segment_filename(void) {
    const char* path = temp_path("verify_bad_segment_name");
    char bad_path[512];
    make_series_file_path(bad_path, sizeof(bad_path), path, "s", "bad_segment.tse");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    FILE* f = fopen(bad_path, "wb");
    CHECK(f != NULL);
    CHECK(fclose(f) == 0);

    tsedge_verify_report report;
    CHECK(tsedge_verify(path, &report) == TSEDGE_ERR_CORRUPT);
    CHECK(report.error_count > 0);
    CHECK(strstr(report.first_error_message, "invalid segment filename") != NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_truncated_segment(void) {
    const char* path = temp_path("verify_truncated_segment");
    char segment_path[512];
    make_series_file_path(segment_path, sizeof(segment_path), path, "s", "segment_000001.tse");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 16; ++i) {
        CHECK_OK(tsedge_append(db, "s", 1000 + i, (double)i));
    }
    CHECK_OK(tsedge_flush_all(db));

    struct stat st;
    CHECK(stat(segment_path, &st) == 0);
    CHECK(st.st_size > 8);
    CHECK(truncate(segment_path, st.st_size - 8) == 0);

    tsedge_verify_report report;
    CHECK(tsedge_verify(path, &report) == TSEDGE_ERR_CORRUPT);
    CHECK(report.error_count > 0);
    CHECK(strstr(report.first_error_message, "payload") != NULL || strstr(report.first_error_message, "truncated") != NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_invalid_args(void) {
    tsedge_verify_report report;
    CHECK(tsedge_verify(NULL, &report) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_verify("/tmp/tsedge_missing", NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
}

void test_series_stats_empty_and_buffered(void) {
    const char* path = temp_path("stats_buffered");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.block_count == 0);
    CHECK(stats.buffered_points == 0);
    CHECK(stats.total_indexed_points == 0);
    CHECK(stats.segment_count == 0);
    CHECK(stats.active_segment_id == 1);
    CHECK(stats.has_time_range == 0);
    CHECK(stats.segment_size_bytes == 0);
    CHECK(stats.total_segment_size_bytes == 0);
    CHECK(stats.raw_size_estimate_bytes == 0);
    CHECK(stats.compressed_size_bytes == 0);
    CHECK(stats.compression_ratio == 0.0);
    CHECK(stats.bytes_per_point == 0.0);

    CHECK_OK(tsedge_append(db, "s", 30, 3.0));
    CHECK_OK(tsedge_append(db, "s", 10, 1.0));
    CHECK_OK(tsedge_append(db, "s", 20, 2.0));
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.block_count == 0);
    CHECK(stats.buffered_points == 3);
    CHECK(stats.total_indexed_points == 0);
    CHECK(stats.segment_count == 0);
    CHECK(stats.active_segment_id == 1);
    CHECK(stats.has_time_range == 1);
    CHECK(stats.min_timestamp == 10);
    CHECK(stats.max_timestamp == 30);
    CHECK(stats.segment_size_bytes == 0);
    CHECK(stats.total_segment_size_bytes == 0);
    CHECK(stats.raw_size_estimate_bytes == (uint64_t)stats.buffered_points * (uint64_t)sizeof(tsedge_point));
    CHECK(stats.compressed_size_bytes == 0);
    CHECK(stats.compression_ratio == 0.0);
    CHECK(stats.bytes_per_point == 0.0);

    CHECK(tsedge_get_series_stats(db, "missing", &stats) == TSEDGE_ERR_NOT_FOUND);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_series_stats_blocks_and_reopen(void) {
    const char* path = temp_path("stats_blocks");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u + 10u;
    tsedge_point* points = (tsedge_point*)malloc(count * sizeof(*points));
    CHECK(points != NULL);
    for (size_t i = 0; i < count; ++i) {
        points[i].timestamp = 1000 + (int64_t)i;
        points[i].value = (double)i;
    }

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.block_count == 2);
    CHECK(stats.buffered_points == 10);
    CHECK(stats.total_indexed_points == (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u);
    CHECK(stats.segment_count >= 1);
    CHECK(stats.active_segment_id >= 1);
    CHECK(stats.has_time_range == 1);
    CHECK(stats.min_timestamp == 1000);
    CHECK(stats.max_timestamp == 1000 + (int64_t)count - 1);
    CHECK(stats.segment_size_bytes > 0);
    CHECK(stats.total_segment_size_bytes == stats.segment_size_bytes);
    CHECK(stats.raw_size_estimate_bytes == (uint64_t)count * (uint64_t)sizeof(tsedge_point));
    CHECK(stats.compressed_size_bytes == stats.total_segment_size_bytes);
    CHECK(stats.compression_ratio > 0.0);
    CHECK(stats.bytes_per_point > 0.0);

    CHECK_OK(tsedge_close(db));
    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.block_count == 3);
    CHECK(stats.buffered_points == 0);
    CHECK(stats.total_indexed_points == count);
    CHECK(stats.segment_count >= 1);
    CHECK(stats.active_segment_id >= 1);
    CHECK(stats.has_time_range == 1);
    CHECK(stats.min_timestamp == 1000);
    CHECK(stats.max_timestamp == 1000 + (int64_t)count - 1);
    CHECK(stats.segment_size_bytes > 0);
    CHECK(stats.total_segment_size_bytes == stats.segment_size_bytes);
    CHECK(stats.raw_size_estimate_bytes == (uint64_t)count * (uint64_t)sizeof(tsedge_point));
    CHECK(stats.compressed_size_bytes == stats.total_segment_size_bytes);
    CHECK(stats.compression_ratio > 0.0);
    CHECK(stats.bytes_per_point > 0.0);
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_series_compression_stats(void) {
    const char* path = temp_path("compression_stats");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "empty"));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "empty", &stats));
    CHECK(stats.raw_size_estimate_bytes == 0);
    CHECK(stats.compressed_size_bytes == 0);
    CHECK(stats.compression_ratio == 0.0);
    CHECK(stats.bytes_per_point == 0.0);

    CHECK_OK(tsedge_create_series(db, "s"));
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS + 32u;
    tsedge_point* points = make_linear_points(count, 5000);
    CHECK(points != NULL);
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.raw_size_estimate_bytes == (uint64_t)count * (uint64_t)sizeof(tsedge_point));
    CHECK(stats.compressed_size_bytes == stats.total_segment_size_bytes);
    CHECK(stats.compressed_size_bytes > 0);
    CHECK(stats.compression_ratio > 0.0);
    CHECK(stats.bytes_per_point > 0.0);

    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_series_stats_after_single_block_flush(void) {
    const char* path = temp_path("stats_one_block");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS + 1u;
    tsedge_point* points = (tsedge_point*)malloc(count * sizeof(*points));
    CHECK(points != NULL);
    for (size_t i = 0; i < count; ++i) {
        points[i].timestamp = (int64_t)i;
        points[i].value = (double)i;
    }

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.block_count == 1);
    CHECK(stats.buffered_points == 1);
    CHECK(stats.total_indexed_points == TSEDGE_BLOCK_MAX_POINTS);
    CHECK(stats.segment_count == 1);
    CHECK(stats.active_segment_id == 1);
    CHECK(stats.has_time_range == 1);
    CHECK(stats.min_timestamp == 0);
    CHECK(stats.max_timestamp == (int64_t)TSEDGE_BLOCK_MAX_POINTS);
    CHECK(stats.segment_size_bytes > 0);
    CHECK(stats.total_segment_size_bytes == stats.segment_size_bytes);

    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_block_stats_aggregate_and_index(void) {
    const char* path = temp_path("block_stats");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 9000; ++i) {
        CHECK_OK(tsedge_append(db, "s", i, (double)i));
    }
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series* series = tsedge_db_find_series(db, "s");
    CHECK(series != NULL);
    CHECK(series->block_index_count == 3);

    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "s", 0, 4095, TSEDGE_AGG_SUM, &result));
    CHECK(result == (4095.0 * 4096.0) / 2.0);
    CHECK_OK(tsedge_aggregate(db, "s", 0, 4095, TSEDGE_AGG_COUNT, &result));
    CHECK(result == 4096.0);
    CHECK_OK(tsedge_aggregate(db, "s", 4090, 4105, TSEDGE_AGG_SUM, &result));
    CHECK(result == ((4090.0 + 4105.0) * 16.0) / 2.0);
    CHECK_OK(tsedge_aggregate(db, "s", 4090, 4105, TSEDGE_AGG_MIN, &result));
    CHECK(result == 4090.0);
    CHECK_OK(tsedge_aggregate(db, "s", 4090, 4105, TSEDGE_AGG_MAX, &result));
    CHECK(result == 4105.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_read_range_callback_stops_full_scan(void) {
    const char* path = temp_path("stop_scan");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 5000; ++i) {
        CHECK_OK(tsedge_append(db, "s", i, (double)i));
    }
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    size_t count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 0, 4999, stop_after_first_cb, &count));
    CHECK(count == 1);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_segment_rotation_creates_multiple_files(void) {
    const char* path = temp_path("segment_rotation");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 4u + 123u;
    tsedge_point* points = make_linear_points(count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_close(db));

    char first_path[512];
    char second_path[512];
    make_series_file_path(first_path, sizeof(first_path), path, "s", "segment_000001.tse");
    make_series_file_path(second_path, sizeof(second_path), path, "s", "segment_000002.tse");
    CHECK(path_exists(first_path));
    CHECK(path_exists(second_path));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count >= 2);
    CHECK(stats.active_segment_id >= 2);
    CHECK(stats.block_count > 0);
    CHECK(stats.total_indexed_points == count);
    CHECK(stats.total_segment_size_bytes == stats.segment_size_bytes);
    CHECK(stats.total_segment_size_bytes > 0);
    CHECK_OK(tsedge_close(db));

    free(points);
    rm_rf(path);
}

void test_read_range_across_segments(void) {
    const char* path = temp_path("segment_read");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 3u;
    tsedge_point* points = make_linear_points(count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count >= 2);

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 4090, 4105, collect_cb, &vec));
    CHECK(vec.count == 16);
    CHECK(vec.points[0].timestamp == 4090);
    CHECK(vec.points[15].timestamp == 4105);
    CHECK(vec.points[0].value == 4090.0);
    CHECK(vec.points[15].value == 4105.0);
    CHECK_OK(tsedge_close(db));

    free(points);
    rm_rf(path);
}

void test_aggregate_across_segments(void) {
    const char* path = temp_path("segment_aggregate");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 4u;
    tsedge_point* points = make_linear_points(count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));

    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "s", 1000, 9000, TSEDGE_AGG_SUM, &result));
    CHECK(result == ((1000.0 + 9000.0) * 8001.0) / 2.0);
    CHECK_OK(tsedge_aggregate(db, "s", 1000, 9000, TSEDGE_AGG_AVG, &result));
    CHECK(result == 5000.0);
    CHECK_OK(tsedge_aggregate(db, "s", 1000, 9000, TSEDGE_AGG_MIN, &result));
    CHECK(result == 1000.0);
    CHECK_OK(tsedge_aggregate(db, "s", 1000, 9000, TSEDGE_AGG_MAX, &result));
    CHECK(result == 9000.0);
    CHECK_OK(tsedge_aggregate(db, "s", 1000, 9000, TSEDGE_AGG_COUNT, &result));
    CHECK(result == 8001.0);
    CHECK_OK(tsedge_close(db));

    free(points);
    rm_rf(path);
}

void test_reopen_rebuilds_multi_segment_index(void) {
    const char* path = temp_path("segment_reopen");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 4u + 33u;
    tsedge_point* points = make_linear_points(count, 1000);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count >= 2);
    CHECK(stats.block_count >= 4);
    CHECK(stats.total_indexed_points == count);

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 1000, 1000 + (int64_t)count - 1, count_cb, &read_count));
    CHECK(read_count == count);

    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "s", 1000, 1000 + (int64_t)count - 1, TSEDGE_AGG_COUNT, &result));
    CHECK(result == (double)count);
    CHECK_OK(tsedge_close(db));

    free(points);
    rm_rf(path);
}

void test_wal_replay_with_segment_rotation(void) {
    const char* path = temp_path("segment_wal");
    size_t flushed_count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 3u;
    tsedge_point* points = make_linear_points(flushed_count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, flushed_count));
    CHECK_OK(tsedge_append(db, "s", (int64_t)flushed_count, 1.0));
    CHECK_OK(tsedge_append(db, "s", (int64_t)flushed_count + 1, 2.0));
    CHECK_OK(tsedge_append(db, "s", (int64_t)flushed_count + 2, 3.0));
    /* Intentionally skip close: the last three points must be recovered from WAL. */
    db = NULL;

    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count >= 2);
    CHECK(stats.buffered_points == 3);

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", (int64_t)flushed_count, (int64_t)flushed_count + 2, count_cb, &read_count));
    CHECK(read_count == 3);
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", (int64_t)flushed_count, (int64_t)flushed_count + 2, count_cb, &read_count));
    CHECK(read_count == 3);
    CHECK_OK(tsedge_close(db));

    free(points);
    rm_rf(path);
}

void test_single_segment_database_still_works(void) {
    const char* path = temp_path("single_segment");
    char csv_path[512];
    snprintf(csv_path, sizeof(csv_path), "%s/out.csv", path);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 100; ++i) {
        CHECK_OK(tsedge_append(db, "s", i, (double)i));
    }
    tsedge_point batch[28];
    for (int i = 0; i < 28; ++i) {
        batch[i].timestamp = 100 + i;
        batch[i].value = 100.0 + (double)i;
    }
    CHECK_OK(tsedge_append_batch(db, "s", batch, 28));

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 10, 19, count_cb, &read_count));
    CHECK(read_count == 10);

    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "s", 0, 127, TSEDGE_AGG_COUNT, &result));
    CHECK(result == 128.0);
    CHECK_OK(tsedge_export_csv(db, "s", 0, 10, csv_path));
    CHECK(path_exists(csv_path));
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count == 1);
    CHECK(stats.block_count == 1);
    CHECK(stats.total_indexed_points == 128);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_delete_before_empty_series(void) {
    const char* path = temp_path("delete_empty");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_delete_before(db, "s", 1000));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count == 0);
    CHECK(stats.block_count == 0);
    CHECK(stats.total_indexed_points == 0);
    CHECK(stats.buffered_points == 0);
    CHECK(stats.has_time_range == 0);
    CHECK(stats.total_segment_size_bytes == 0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_delete_before_no_matching_segments(void) {
    const char* path = temp_path("delete_none");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 3u;
    tsedge_point* points = make_linear_points(count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));

    tsedge_series_stats before;
    CHECK_OK(tsedge_get_series_stats(db, "s", &before));
    CHECK_OK(tsedge_delete_before(db, "s", 0));

    tsedge_series_stats after;
    CHECK_OK(tsedge_get_series_stats(db, "s", &after));
    CHECK(after.segment_count >= before.segment_count);
    CHECK(after.block_count >= before.block_count);
    CHECK(after.total_indexed_points == count);
    CHECK(after.buffered_points == 0);

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 0, (int64_t)count - 1, count_cb, &read_count));
    CHECK(read_count == count);
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_delete_before_removes_one_segment(void) {
    const char* path = temp_path("delete_one");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 3u;
    tsedge_point* points = make_linear_points(count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_delete_before(db, "s", (int64_t)TSEDGE_BLOCK_MAX_POINTS));

    char first_path[512];
    char second_path[512];
    make_series_file_path(first_path, sizeof(first_path), path, "s", "segment_000001.tse");
    make_series_file_path(second_path, sizeof(second_path), path, "s", "segment_000002.tse");
    CHECK(!path_exists(first_path));
    CHECK(path_exists(second_path));

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 0, (int64_t)TSEDGE_BLOCK_MAX_POINTS - 1, count_cb, &read_count));
    CHECK(read_count == 0);
    CHECK_OK(tsedge_read_range(db, "s", (int64_t)TSEDGE_BLOCK_MAX_POINTS, (int64_t)count - 1, count_cb, &read_count));
    CHECK(read_count == count - (size_t)TSEDGE_BLOCK_MAX_POINTS);
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_delete_before_removes_multiple_segments(void) {
    const char* path = temp_path("delete_many");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 4u;
    tsedge_point* points = make_linear_points(count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_delete_before(db, "s", (int64_t)TSEDGE_BLOCK_MAX_POINTS * 2));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count == 2);
    CHECK(stats.block_count == 2);
    CHECK(stats.total_indexed_points == count - (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u);
    CHECK(stats.min_timestamp == (int64_t)TSEDGE_BLOCK_MAX_POINTS * 2);

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 0, (int64_t)count - 1, count_cb, &read_count));
    CHECK(read_count == count - (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u);

    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "s", 0, (int64_t)count - 1, TSEDGE_AGG_COUNT, &result));
    CHECK(result == (double)(count - (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u));
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_delete_before_keeps_partial_segment(void) {
    const char* path = temp_path("delete_partial");
    size_t count = 1000u;
    tsedge_point* points = make_linear_points(count, 1000);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_delete_before(db, "s", 1500));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count == 1);
    CHECK(stats.block_count == 1);
    CHECK(stats.total_indexed_points == count);

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 1000, 1499, count_cb, &read_count));
    CHECK(read_count == 500);
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_delete_before_reopen(void) {
    const char* path = temp_path("delete_reopen");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 4u;
    tsedge_point* points = make_linear_points(count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_delete_before(db, "s", (int64_t)TSEDGE_BLOCK_MAX_POINTS * 2));
    CHECK_OK(tsedge_close(db));

    char deleted_path[512];
    make_series_file_path(deleted_path, sizeof(deleted_path), path, "s", "segment_000001.tse");
    CHECK(!path_exists(deleted_path));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count == 2);
    CHECK(stats.block_count == 2);

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 0, (int64_t)count - 1, count_cb, &read_count));
    CHECK(read_count == count - (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u);
    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "s", 0, (int64_t)count - 1, TSEDGE_AGG_COUNT, &result));
    CHECK(result == (double)read_count);
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_delete_before_append_after_delete(void) {
    const char* path = temp_path("delete_append");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 3u;
    tsedge_point* points = make_linear_points(count, 0);
    tsedge_point* newer = make_linear_points((size_t)TSEDGE_BLOCK_MAX_POINTS * 2u, 20000);
    CHECK(points != NULL);
    CHECK(newer != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_delete_before(db, "s", (int64_t)TSEDGE_BLOCK_MAX_POINTS * 2));
    CHECK_OK(tsedge_append_batch(db, "s", newer, (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u));
    CHECK_OK(tsedge_close(db));

    char first_path[512];
    char fourth_path[512];
    make_series_file_path(first_path, sizeof(first_path), path, "s", "segment_000001.tse");
    make_series_file_path(fourth_path, sizeof(fourth_path), path, "s", "segment_000004.tse");
    CHECK(!path_exists(first_path));
    CHECK(path_exists(fourth_path));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 20000, 20000 + (int64_t)TSEDGE_BLOCK_MAX_POINTS * 2 - 1, count_cb, &read_count));
    CHECK(read_count == (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u);
    CHECK_OK(tsedge_close(db));
    free(points);
    free(newer);
    rm_rf(path);
}

void test_delete_before_wal_consistency(void) {
    const char* path = temp_path("delete_wal");
    size_t flushed_count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 3u;
    tsedge_point* points = make_linear_points(flushed_count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, flushed_count));
    CHECK_OK(tsedge_append(db, "s", (int64_t)flushed_count, 1.0));
    CHECK_OK(tsedge_append(db, "s", (int64_t)flushed_count + 1, 2.0));
    CHECK_OK(tsedge_append(db, "s", (int64_t)flushed_count + 2, 3.0));
    CHECK_OK(tsedge_delete_before(db, "s", (int64_t)TSEDGE_BLOCK_MAX_POINTS * 2));

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.buffered_points == 0);
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 0, (int64_t)TSEDGE_BLOCK_MAX_POINTS * 2 - 1, count_cb, &read_count));
    CHECK(read_count == 0);
    CHECK_OK(tsedge_read_range(db, "s", (int64_t)flushed_count, (int64_t)flushed_count + 2, count_cb, &read_count));
    CHECK(read_count == 3);
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_multiple_series(void) {
    const char* path = temp_path("multi");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "a"));
    CHECK_OK(tsedge_create_series(db, "b"));
    CHECK_OK(tsedge_append(db, "a", 1, 1.0));
    CHECK_OK(tsedge_append(db, "b", 1, 2.0));
    double result = 0.0;
    CHECK_OK(tsedge_aggregate(db, "a", 1, 1, TSEDGE_AGG_SUM, &result));
    CHECK(result == 1.0);
    CHECK_OK(tsedge_aggregate(db, "b", 1, 1, TSEDGE_AGG_SUM, &result));
    CHECK(result == 2.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}
