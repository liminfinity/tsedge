#include "db.h"
#include "tsedge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    CHECK(stats.has_time_range == 0);
    CHECK(stats.segment_size_bytes == 0);

    CHECK_OK(tsedge_append(db, "s", 30, 3.0));
    CHECK_OK(tsedge_append(db, "s", 10, 1.0));
    CHECK_OK(tsedge_append(db, "s", 20, 2.0));
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.block_count == 0);
    CHECK(stats.buffered_points == 3);
    CHECK(stats.total_indexed_points == 0);
    CHECK(stats.has_time_range == 1);
    CHECK(stats.min_timestamp == 10);
    CHECK(stats.max_timestamp == 30);
    CHECK(stats.segment_size_bytes == 0);

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
    CHECK(stats.has_time_range == 1);
    CHECK(stats.min_timestamp == 1000);
    CHECK(stats.max_timestamp == 1000 + (int64_t)count - 1);
    CHECK(stats.segment_size_bytes > 0);

    CHECK_OK(tsedge_close(db));
    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.block_count == 3);
    CHECK(stats.buffered_points == 0);
    CHECK(stats.total_indexed_points == count);
    CHECK(stats.has_time_range == 1);
    CHECK(stats.min_timestamp == 1000);
    CHECK(stats.max_timestamp == 1000 + (int64_t)count - 1);
    CHECK(stats.segment_size_bytes > 0);
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
    CHECK(stats.has_time_range == 1);
    CHECK(stats.min_timestamp == 0);
    CHECK(stats.max_timestamp == (int64_t)TSEDGE_BLOCK_MAX_POINTS);
    CHECK(stats.segment_size_bytes > 0);

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
