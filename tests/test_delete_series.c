#include "test_helpers.h"

#include <string.h>

static int list_contains(tsedge_series_info* list, size_t count, const char* name) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(list[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int append_points(tsedge_db* db, const char* series_name, int64_t base, int count) {
    for (int i = 0; i < count; ++i) {
        int rc = tsedge_append(db, series_name, base + i, 10.0 + (double)i);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return TSEDGE_OK;
}

void test_delete_series_basic(void) {
    const char* path = temp_path("delete_series_basic");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(append_points(db, "air.temperature", 1000, 8));
    CHECK_OK(tsedge_flush_all(db));

    CHECK_OK(tsedge_delete_series(db, "air.temperature"));
    size_t count = 0;
    tsedge_series_info* list = NULL;
    CHECK_OK(tsedge_list_series(db, &list, &count));
    CHECK(!list_contains(list, count, "air.temperature"));
    tsedge_free_series_list(list);

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK(tsedge_read_range(db, "air.temperature", 1000, 2000, collect_cb, &vec) == TSEDGE_ERR_NOT_FOUND);
    CHECK(vec.count == 0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_delete_series_files_removed(void) {
    const char* path = temp_path("delete_series_files");
    char series_dir[512];
    snprintf(series_dir, sizeof(series_dir), "%s/series/air.temperature", path);
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(append_points(db, "air.temperature", 1000, 8));
    CHECK_OK(tsedge_flush_all(db));
    CHECK(path_exists(series_dir));

    CHECK_OK(tsedge_delete_series(db, "air.temperature"));
    CHECK(!path_exists(series_dir));
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_delete_series_after_reopen(void) {
    const char* path = temp_path("delete_series_reopen");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(append_points(db, "air.temperature", 1000, 8));
    CHECK_OK(tsedge_delete_series(db, "air.temperature"));
    CHECK_OK(tsedge_close(db));

    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_info* list = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_list_series(db, &list, &count));
    CHECK(!list_contains(list, count, "air.temperature"));
    tsedge_free_series_list(list);
    CHECK(tsedge_read_range(db, "air.temperature", 1000, 2000, collect_cb, NULL) == TSEDGE_ERR_NOT_FOUND);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_delete_series_with_pending_wal(void) {
    const char* path = temp_path("delete_series_pending_wal");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(append_points(db, "air.temperature", 1000, 8));

    CHECK_OK(tsedge_delete_series(db, "air.temperature"));
    CHECK_OK(tsedge_close(db));

    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_info* list = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_list_series(db, &list, &count));
    CHECK(!list_contains(list, count, "air.temperature"));
    tsedge_free_series_list(list);
    CHECK(tsedge_read_range(db, "air.temperature", 1000, 2000, collect_cb, NULL) == TSEDGE_ERR_NOT_FOUND);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_delete_one_series_keeps_others(void) {
    const char* path = temp_path("delete_one_keeps_others");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(tsedge_create_series(db, "air.humidity"));
    CHECK_OK(tsedge_create_series(db, "pm25.concentration"));
    CHECK_OK(append_points(db, "air.temperature", 1000, 4));
    CHECK_OK(append_points(db, "air.humidity", 1000, 4));
    CHECK_OK(append_points(db, "pm25.concentration", 1000, 4));

    CHECK_OK(tsedge_delete_series(db, "air.humidity"));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "air.temperature", 1000, 2000, collect_cb, &vec));
    CHECK(vec.count == 4);
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "pm25.concentration", 1000, 2000, collect_cb, &vec));
    CHECK(vec.count == 4);
    CHECK(tsedge_read_range(db, "air.humidity", 1000, 2000, collect_cb, &vec) == TSEDGE_ERR_NOT_FOUND);

    tsedge_series_info* list = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_list_series(db, &list, &count));
    CHECK(count == 2);
    CHECK(list_contains(list, count, "air.temperature"));
    CHECK(list_contains(list, count, "pm25.concentration"));
    CHECK(!list_contains(list, count, "air.humidity"));
    tsedge_free_series_list(list);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_delete_series_invalid_args(void) {
    const char* path = temp_path("delete_series_invalid");
    tsedge_db* db = NULL;
    CHECK(tsedge_delete_series(NULL, "x") == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK_OK(tsedge_open(path, &db));
    CHECK(tsedge_delete_series(db, NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_delete_series(db, "") == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_delete_series(db, "missing.series") == TSEDGE_ERR_NOT_FOUND);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_delete_series_then_recreate(void) {
    const char* path = temp_path("delete_series_recreate");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(append_points(db, "air.temperature", 1000, 4));
    CHECK_OK(tsedge_delete_series(db, "air.temperature"));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(tsedge_append(db, "air.temperature", 2000, 42.0));
    CHECK_OK(tsedge_close(db));

    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "air.temperature", 1, 3000, collect_cb, &vec));
    CHECK(vec.count == 1);
    CHECK(vec.points[0].timestamp == 2000);
    CHECK(vec.points[0].value == 42.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}
