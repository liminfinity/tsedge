#include "test_helpers.h"
#include "tsedge.h"

#include <string.h>

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
