#define _POSIX_C_SOURCE 200809L

#include "test_helpers.h"
#include "tsedge.h"

#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint64_t test_file_size_or_zero(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        return 0;
    }
    return (uint64_t)st.st_size;
}

static uint64_t test_db_size(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        return 0;
    }

    uint64_t total = 0;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(child, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            total += test_db_size(child);
        } else if (S_ISREG(st.st_mode) && st.st_size > 0) {
            total += (uint64_t)st.st_size;
        }
    }
    closedir(dir);
    return total;
}

static void make_segment_path(char* out, size_t out_size, const char* db_path, const char* series, uint32_t segment_id) {
    snprintf(out, out_size, "%s/series/%s/segment_%06u.tse", db_path, series, segment_id);
}

static tsedge_point* make_quota_points(size_t count) {
    return make_linear_points(count, 0);
}

void test_disk_quota_disabled_and_get_set(void) {
    const char* path = temp_path("quota_disabled");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));

    uint64_t quota = 123u;
    CHECK_OK(tsedge_get_disk_quota(db, &quota));
    CHECK(quota == 0);
    CHECK_OK(tsedge_set_disk_quota(db, 0));
    CHECK_OK(tsedge_enforce_disk_quota(db));
    CHECK_OK(tsedge_set_disk_quota(db, 1024));
    CHECK_OK(tsedge_get_disk_quota(db, &quota));
    CHECK(quota == 1024);

    CHECK(tsedge_set_disk_quota(NULL, 1) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_get_disk_quota(NULL, &quota) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_get_disk_quota(db, NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_enforce_disk_quota(NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(strcmp(tsedge_strerror(TSEDGE_ERR_QUOTA_EXCEEDED), "disk quota exceeded") == 0);

    CHECK_OK(tsedge_set_disk_quota(db, 0));
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_disk_quota_under_limit_no_delete(void) {
    const char* path = temp_path("quota_under");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS;
    tsedge_point* points = make_quota_points(count);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_flush_all(db));

    tsedge_series_stats before;
    CHECK_OK(tsedge_get_series_stats(db, "s", &before));
    CHECK(before.segment_count == 1);

    CHECK_OK(tsedge_set_disk_quota(db, test_db_size(path) + 4096u));
    CHECK_OK(tsedge_enforce_disk_quota(db));

    tsedge_series_stats after;
    CHECK_OK(tsedge_get_series_stats(db, "s", &after));
    CHECK(after.segment_count == before.segment_count);
    CHECK(after.block_count == before.block_count);
    CHECK(after.total_indexed_points == before.total_indexed_points);

    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_disk_quota_removes_old_segments_and_updates_index(void) {
    const char* path = temp_path("quota_remove");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 4u;
    tsedge_point* points = make_quota_points(count);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_flush_all(db));

    tsedge_series_stats before;
    CHECK_OK(tsedge_get_series_stats(db, "s", &before));
    CHECK(before.segment_count >= 3);

    char first_path[512];
    make_segment_path(first_path, sizeof(first_path), path, "s", 1u);
    uint64_t first_size = test_file_size_or_zero(first_path);
    CHECK(first_size > 0);
    uint64_t current_size = test_db_size(path);
    CHECK(current_size > first_size);

    CHECK_OK(tsedge_set_disk_quota(db, current_size - first_size / 2u));
    CHECK_OK(tsedge_enforce_disk_quota(db));
    CHECK(!path_exists(first_path));

    tsedge_series_stats after;
    CHECK_OK(tsedge_get_series_stats(db, "s", &after));
    CHECK(after.segment_count < before.segment_count);
    CHECK(after.block_count < before.block_count);
    CHECK(after.total_indexed_points < before.total_indexed_points);

    size_t read_count = 0;
    CHECK_OK(tsedge_read_range(db, "s", 0, (int64_t)TSEDGE_BLOCK_MAX_POINTS - 1, count_cb, &read_count));
    CHECK(read_count == 0);
    CHECK_OK(tsedge_read_range(db, "s", (int64_t)TSEDGE_BLOCK_MAX_POINTS, (int64_t)count - 1, count_cb, &read_count));
    CHECK(read_count == after.total_indexed_points);

    tsedge_verify_report report;
    CHECK_OK(tsedge_verify(path, &report));
    CHECK(report.error_count == 0);
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_get_series_stats(db, "s", &after));
    CHECK(after.total_indexed_points < before.total_indexed_points);
    CHECK_OK(tsedge_close(db));

    free(points);
    rm_rf(path);
}

void test_disk_quota_keeps_active_and_last_segment(void) {
    const char* path = temp_path("quota_last");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS;
    tsedge_point* points = make_quota_points(count);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_flush_all(db));

    char first_path[512];
    make_segment_path(first_path, sizeof(first_path), path, "s", 1u);
    CHECK(path_exists(first_path));

    CHECK_OK(tsedge_set_disk_quota(db, 1));
    CHECK(tsedge_enforce_disk_quota(db) == TSEDGE_ERR_QUOTA_EXCEEDED);
    CHECK(path_exists(first_path));
    CHECK(tsedge_append(db, "s", (int64_t)count, 1.0) == TSEDGE_ERR_QUOTA_EXCEEDED);

    tsedge_series_stats stats;
    CHECK_OK(tsedge_get_series_stats(db, "s", &stats));
    CHECK(stats.segment_count == 1);
    CHECK(stats.block_count == 1);
    CHECK(tsedge_close(db) == TSEDGE_ERR_QUOTA_EXCEEDED);

    free(points);
    rm_rf(path);
}

void test_disk_quota_append_batch_reports_exceeded(void) {
    const char* path = temp_path("quota_append_batch");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS;
    tsedge_point* points = make_quota_points(count);
    CHECK(points != NULL);
    tsedge_point extra = {(int64_t)count, 1.0};

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_set_disk_quota(db, 1));
    CHECK(tsedge_append_batch(db, "s", &extra, 1) == TSEDGE_ERR_QUOTA_EXCEEDED);
    CHECK(tsedge_close(db) == TSEDGE_ERR_QUOTA_EXCEEDED);

    free(points);
    rm_rf(path);
}

void test_disk_quota_handle_appends_report_exceeded(void) {
    const char* path = temp_path("quota_handle");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS;
    tsedge_point* points = make_quota_points(count);
    tsedge_point* batch_points = make_linear_points(count + 1u, (int64_t)count + 1);
    CHECK(points != NULL);
    CHECK(batch_points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    tsedge_series_handle* handle = NULL;
    CHECK_OK(tsedge_get_series_handle(db, "s", &handle));
    CHECK_OK(tsedge_append_batch_handle(db, handle, points, count));
    CHECK_OK(tsedge_set_disk_quota(db, 1));
    CHECK(tsedge_append_handle(db, handle, (int64_t)count, 1.0) == TSEDGE_ERR_QUOTA_EXCEEDED);
    CHECK(tsedge_append_batch_handle(db, handle, batch_points, count + 1u) == TSEDGE_ERR_QUOTA_EXCEEDED);
    CHECK(tsedge_close(db) == TSEDGE_ERR_QUOTA_EXCEEDED);

    free(points);
    free(batch_points);
    rm_rf(path);
}
