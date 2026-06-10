#include "block.h"
#include "test_helpers.h"
#include "tsedge.h"

#include <stdlib.h>

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
