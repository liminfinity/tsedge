#include "block.h"
#include "db.h"
#include "test_helpers.h"
#include "tsedge.h"

#include <stdlib.h>
#include <string.h>

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
