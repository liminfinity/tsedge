#ifndef TSEDGE_TEST_HELPERS_H
#define TSEDGE_TEST_HELPERS_H

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

/* Collects points into a fixed-size test vector. */
static int collect_cb(const tsedge_point* point, void* user_data) {
    point_vec* vec = (point_vec*)user_data;
    if (vec->count < sizeof(vec->points) / sizeof(vec->points[0])) {
        vec->points[vec->count++] = *point;
    }
    return 0;
}

/* Stops a range read after the first callback invocation. */
static int stop_after_first_cb(const tsedge_point* point, void* user_data) {
    (void)point;
    size_t* count = (size_t*)user_data;
    ++(*count);
    return 1;
}

/* Counts points seen by a streaming range read. */
static int count_cb(const tsedge_point* point, void* user_data) {
    (void)point;
    size_t* count = (size_t*)user_data;
    ++(*count);
    return 0;
}

/* Counts data rows in a generated CSV file, excluding the header. */
static size_t count_csv_data_rows(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    char buf[256];
    size_t rows = 0;
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0;
    }
    while (fgets(buf, sizeof(buf), f)) {
        ++rows;
    }
    fclose(f);
    return rows;
}

/* Removes a temporary test directory tree. */
static void rm_rf(const char* path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    int rc = system(cmd);
    (void)rc;
}

/* Builds a process-local temporary database path and clears it first. */
static const char* temp_path(const char* name) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/tsedge_%s_%ld", name, (long)getpid());
    rm_rf(path);
    return path;
}

/* Checks whether a path exists on disk. */
static int path_exists(const char* path) {
    return access(path, F_OK) == 0;
}

/* Builds a path to a file inside one series directory. */
static void make_series_file_path(char* out, size_t out_size, const char* db_path, const char* series_name, const char* filename) {
    snprintf(out, out_size, "%s/series/%s/%s", db_path, series_name, filename);
}

/* Allocates a simple increasing point sequence for storage tests. */
static tsedge_point* make_linear_points(size_t count, int64_t timestamp_base) {
    tsedge_point* points = (tsedge_point*)malloc(count * sizeof(*points));
    if (!points) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        points[i].timestamp = timestamp_base + (int64_t)i;
        points[i].value = (double)i;
    }
    return points;
}

#endif
