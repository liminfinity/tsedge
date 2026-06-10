#include "block.h"
#include "test_helpers.h"
#include "tsedge.h"

#include <stdlib.h>
#include <string.h>

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
