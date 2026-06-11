#include "test_helpers.h"
#include "tsedge.h"

#include <stdlib.h>

static void check_window(
    const tsedge_window_aggregate* window,
    int64_t start,
    int64_t end,
    uint64_t count,
    double min_value,
    double max_value,
    double avg
) {
    CHECK(window->window_start == start);
    CHECK(window->window_end == end);
    CHECK(window->count == count);
    CHECK(window->min == min_value);
    CHECK(window->max == max_value);
    CHECK(window->avg == avg);
}

void test_window_aggregate_basic(void) {
    const char* path = temp_path("window_basic");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int64_t i = 0; i < 10; ++i) {
        CHECK_OK(tsedge_append(db, "s", i, (double)i));
    }

    tsedge_window_aggregate* windows = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_aggregate_windowed(db, "s", 0, 10, 5, &windows, &count));
    CHECK(count == 2);
    check_window(&windows[0], 0, 5, 5, 0.0, 4.0, 2.0);
    check_window(&windows[1], 5, 10, 5, 5.0, 9.0, 7.0);
    tsedge_free_window_aggregates(windows);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_window_aggregate_boundary(void) {
    const char* path = temp_path("window_boundary");
    tsedge_point points[] = {
        {0, 0.0},
        {99, 99.0},
        {100, 100.0},
        {199, 199.0},
        {200, 200.0},
    };
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, sizeof(points) / sizeof(points[0])));

    tsedge_window_aggregate* windows = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_aggregate_windowed(db, "s", 0, 300, 100, &windows, &count));
    CHECK(count == 3);
    check_window(&windows[0], 0, 100, 2, 0.0, 99.0, 49.5);
    check_window(&windows[1], 100, 200, 2, 100.0, 199.0, 149.5);
    check_window(&windows[2], 200, 300, 1, 200.0, 200.0, 200.0);
    tsedge_free_window_aggregates(windows);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_window_aggregate_sparse(void) {
    const char* path = temp_path("window_sparse");
    tsedge_point points[] = {
        {0, 10.0},
        {250, 20.0},
        {450, 30.0},
    };
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, sizeof(points) / sizeof(points[0])));

    tsedge_window_aggregate* windows = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_aggregate_windowed(db, "s", 0, 500, 100, &windows, &count));
    CHECK(count == 3);
    check_window(&windows[0], 0, 100, 1, 10.0, 10.0, 10.0);
    check_window(&windows[1], 200, 300, 1, 20.0, 20.0, 20.0);
    check_window(&windows[2], 400, 500, 1, 30.0, 30.0, 30.0);
    tsedge_free_window_aggregates(windows);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_window_aggregate_empty_range(void) {
    const char* path = temp_path("window_empty");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append(db, "s", 10, 1.0));

    tsedge_window_aggregate* windows = (tsedge_window_aggregate*)0x1;
    size_t count = 99;
    CHECK_OK(tsedge_aggregate_windowed(db, "s", 100, 200, 10, &windows, &count));
    CHECK(windows == NULL);
    CHECK(count == 0);

    windows = (tsedge_window_aggregate*)0x1;
    count = 99;
    CHECK_OK(tsedge_aggregate_windowed(db, "s", 10, 10, 10, &windows, &count));
    CHECK(windows == NULL);
    CHECK(count == 0);
    tsedge_free_window_aggregates(NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_window_aggregate_invalid_args(void) {
    const char* path = temp_path("window_invalid");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));

    tsedge_window_aggregate* windows = NULL;
    size_t count = 0;
    CHECK(tsedge_aggregate_windowed(NULL, "s", 0, 10, 1, &windows, &count) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_aggregate_windowed(db, NULL, 0, 10, 1, &windows, &count) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_aggregate_windowed(db, "s", 0, 10, 1, NULL, &count) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_aggregate_windowed(db, "s", 0, 10, 1, &windows, NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_aggregate_windowed(db, "s", 0, 10, 0, &windows, &count) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_aggregate_windowed(db, "s", 0, 10, -1, &windows, &count) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_aggregate_windowed(db, "s", 10, 0, 1, &windows, &count) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_aggregate_windowed(db, "missing", 0, 10, 1, &windows, &count) == TSEDGE_ERR_NOT_FOUND);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_window_aggregate_after_reopen(void) {
    const char* path = temp_path("window_reopen");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int64_t i = 0; i < 20; ++i) {
        CHECK_OK(tsedge_append(db, "s", i, (double)i));
    }
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    tsedge_window_aggregate* windows = NULL;
    size_t count = 0;
    CHECK_OK(tsedge_aggregate_windowed(db, "s", 0, 20, 10, &windows, &count));
    CHECK(count == 2);
    check_window(&windows[0], 0, 10, 10, 0.0, 9.0, 4.5);
    check_window(&windows[1], 10, 20, 10, 10.0, 19.0, 14.5);
    tsedge_free_window_aggregates(windows);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_window_aggregate_multi_block(void) {
    const char* path = temp_path("window_multi_block");
    size_t count = (size_t)TSEDGE_BLOCK_MAX_POINTS * 2u + 10u;
    tsedge_point* points = make_linear_points(count, 0);
    CHECK(points != NULL);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, count));
    CHECK_OK(tsedge_flush_all(db));

    tsedge_window_aggregate* windows = NULL;
    size_t window_count = 0;
    CHECK_OK(tsedge_aggregate_windowed(db, "s", 0, (int64_t)count, (int64_t)TSEDGE_BLOCK_MAX_POINTS, &windows, &window_count));
    CHECK(window_count == 3);
    CHECK(windows[0].count == (uint64_t)TSEDGE_BLOCK_MAX_POINTS);
    CHECK(windows[1].count == (uint64_t)TSEDGE_BLOCK_MAX_POINTS);
    CHECK(windows[2].count == 10u);
    CHECK(windows[0].min == 0.0);
    CHECK(windows[1].min == (double)TSEDGE_BLOCK_MAX_POINTS);
    CHECK(windows[2].min == (double)((size_t)TSEDGE_BLOCK_MAX_POINTS * 2u));
    CHECK(windows[2].max == (double)(count - 1u));
    tsedge_free_window_aggregates(windows);

    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}
