#include "test_helpers.h"
#include "tsedge.h"

#include <string.h>

void test_get_series_handle(void) {
    const char* path = temp_path("handle_get");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));

    tsedge_series_handle* handle = NULL;
    CHECK_OK(tsedge_get_series_handle(db, "air.temperature", &handle));
    CHECK(handle != NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_append_handle_basic(void) {
    const char* path = temp_path("handle_append");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));

    tsedge_series_handle* handle = NULL;
    CHECK_OK(tsedge_get_series_handle(db, "air.temperature", &handle));
    for (int i = 0; i < 5; ++i) {
        CHECK_OK(tsedge_append_handle(db, handle, 1000 + i, 20.0 + (double)i));
    }

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "air.temperature", 1000, 1004, collect_cb, &vec));
    CHECK(vec.count == 5);
    CHECK(vec.points[0].timestamp == 1000);
    CHECK(vec.points[4].value == 24.0);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_append_batch_handle_basic(void) {
    const char* path = temp_path("handle_batch");
    tsedge_point points[4] = {
        {1, 11.0},
        {2, 12.0},
        {3, 13.0},
        {4, 14.0},
    };

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));

    tsedge_series_handle* handle = NULL;
    CHECK_OK(tsedge_get_series_handle(db, "air.temperature", &handle));
    CHECK_OK(tsedge_append_batch_handle(db, handle, points, 4));

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "air.temperature", 1, 4, collect_cb, &vec));
    CHECK(vec.count == 4);
    CHECK(vec.points[2].timestamp == 3);
    CHECK(vec.points[2].value == 13.0);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_handle_invalid_args(void) {
    const char* path = temp_path("handle_invalid");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));

    tsedge_series_handle* handle = NULL;
    tsedge_point point = {1, 1.0};
    CHECK(tsedge_get_series_handle(NULL, "s", &handle) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_get_series_handle(db, NULL, &handle) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_get_series_handle(db, "", &handle) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_get_series_handle(db, "s", NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK_OK(tsedge_get_series_handle(db, "s", &handle));
    CHECK(tsedge_append_handle(NULL, handle, 1, 1.0) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_append_handle(db, NULL, 1, 1.0) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_append_batch_handle(db, handle, NULL, 1) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK_OK(tsedge_append_batch_handle(db, handle, &point, 0));

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_handle_series_not_found(void) {
    const char* path = temp_path("handle_missing");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));

    tsedge_series_handle* handle = NULL;
    CHECK(tsedge_get_series_handle(db, "missing.series", &handle) == TSEDGE_ERR_NOT_FOUND);
    CHECK(handle == NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_handle_after_reopen(void) {
    const char* path = temp_path("handle_reopen");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_handle* handle = NULL;
    CHECK_OK(tsedge_get_series_handle(db, "air.temperature", &handle));
    CHECK_OK(tsedge_append_handle(db, handle, 1, 21.0));

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "air.temperature", 1, 1, collect_cb, &vec));
    CHECK(vec.count == 1);
    CHECK(vec.points[0].value == 21.0);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_handle_after_delete_documented_behavior(void) {
    const char* path = temp_path("handle_delete");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "debug.temp"));

    tsedge_series_handle* handle = NULL;
    CHECK_OK(tsedge_get_series_handle(db, "debug.temp", &handle));
    tsedge_series_handle* old_handle = handle;
    CHECK_OK(tsedge_delete_series(db, "debug.temp"));
    CHECK(tsedge_get_series_handle(db, "debug.temp", &handle) == TSEDGE_ERR_NOT_FOUND);
    CHECK(handle == NULL);
    CHECK(tsedge_append_handle(db, old_handle, 1, 1.0) == TSEDGE_ERR_NOT_FOUND);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}
