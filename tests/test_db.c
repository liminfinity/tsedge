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
