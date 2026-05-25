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

static void rm_rf(const char* path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    (void)system(cmd);
}

static const char* temp_path(const char* name) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/tsedge_%s_%ld", name, (long)getpid());
    rm_rf(path);
    return path;
}

void test_wal_recovery(void) {
    const char* path = temp_path("recovery");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "recover"));
    CHECK_OK(tsedge_append(db, "recover", 1, 11.0));
    CHECK_OK(tsedge_append(db, "recover", 2, 12.0));

    /*
     * Simulate a crash by intentionally not calling tsedge_close. The next open
     * should replay WAL entries for points that were accepted but not flushed.
     */
    db = NULL;

    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "recover", 1, 2, collect_cb, &vec));
    CHECK(vec.count == 2);
    CHECK(vec.points[0].value == 11.0);
    CHECK(vec.points[1].value == 12.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}
