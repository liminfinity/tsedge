#include "test_helpers.h"
#include "tsedge.h"

#include <string.h>

static int list_contains(const tsedge_series_info* list, size_t count, const char* name) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(list[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static const tsedge_series_info* find_info(const tsedge_series_info* list, size_t count, const char* name) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(list[i].name, name) == 0) {
            return &list[i];
        }
    }
    return NULL;
}

void test_list_series_empty_database(void) {
    const char* path = temp_path("list_empty");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));

    tsedge_series_info* list = (tsedge_series_info*)0x1;
    size_t count = 99;
    CHECK_OK(tsedge_list_series(db, &list, &count));
    CHECK(list == NULL);
    CHECK(count == 0);
    tsedge_free_series_list(list);
    tsedge_free_series_list(NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_list_series_multiple_series(void) {
    const char* path = temp_path("list_multiple");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(tsedge_create_series(db, "air.humidity"));
    CHECK_OK(tsedge_create_series(db, "pm25.concentration"));

    tsedge_series_info* list = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_list_series(db, &list, &count));
    CHECK(count == 3);
    CHECK(list != NULL);
    CHECK(list_contains(list, count, "air.temperature"));
    CHECK(list_contains(list, count, "air.humidity"));
    CHECK(list_contains(list, count, "pm25.concentration"));
    tsedge_free_series_list(list);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_list_series_stats_fields(void) {
    const char* path = temp_path("list_stats");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 32; ++i) {
        CHECK_OK(tsedge_append(db, "s", 1000 + i, 10.0 + (double)i));
    }
    CHECK_OK(tsedge_flush_all(db));

    tsedge_series_info* list = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_list_series(db, &list, &count));
    CHECK(count == 1);
    const tsedge_series_info* info = find_info(list, count, "s");
    CHECK(info != NULL);
    CHECK(info->total_points == 32);
    CHECK(info->block_count > 0);
    CHECK(info->segment_count > 0);
    CHECK(info->compressed_size_bytes > 0);
    tsedge_free_series_list(list);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_list_series_invalid_args(void) {
    const char* path = temp_path("list_invalid");
    tsedge_db* db = NULL;
    tsedge_series_info* list = NULL;
    size_t count = 0;

    CHECK(tsedge_list_series(NULL, &list, &count) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK_OK(tsedge_open(path, &db));
    CHECK(tsedge_list_series(db, NULL, &count) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_list_series(db, &list, NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
    tsedge_free_series_list(NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_list_series_after_reopen(void) {
    const char* path = temp_path("list_reopen");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "air.temperature"));
    CHECK_OK(tsedge_create_series(db, "air.humidity"));
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_series_info* list = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_list_series(db, &list, &count));
    CHECK(count == 2);
    CHECK(list_contains(list, count, "air.temperature"));
    CHECK(list_contains(list, count, "air.humidity"));
    tsedge_free_series_list(list);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}
