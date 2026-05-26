#include "tsedge.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static void make_path(char* out, size_t out_size, const char* dir, const char* name) {
    snprintf(out, out_size, "%s/%s", dir, name);
}

static long file_size_or_negative(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static uint32_t read_u32_le_local(const unsigned char* data) {
    return (uint32_t)data[0] |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) |
        ((uint32_t)data[3] << 24);
}

static uint32_t first_wal_entry_size(const char* wal_path) {
    FILE* f = fopen(wal_path, "rb");
    if (!f) {
        return 0;
    }
    unsigned char prefix[12];
    if (fread(prefix, 1, sizeof(prefix), f) != sizeof(prefix)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return read_u32_le_local(prefix + 8);
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

void test_wal_empty_replay(void) {
    const char* path = temp_path("wal_empty");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "empty"));
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_wal_incomplete_last_entry(void) {
    const char* path = temp_path("wal_partial");
    char wal_path[256];
    make_path(wal_path, sizeof(wal_path), path, "wal.log");

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "recover"));
    CHECK_OK(tsedge_append(db, "recover", 1, 11.0));
    CHECK_OK(tsedge_append(db, "recover", 2, 12.0));

    uint32_t first_size = first_wal_entry_size(wal_path);
    CHECK(first_size > 0);
    CHECK(truncate(wal_path, (off_t)first_size + 10) == 0);
    db = NULL;

    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "recover", 1, 2, collect_cb, &vec));
    CHECK(vec.count == 1);
    CHECK(vec.points[0].timestamp == 1);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_wal_bad_checksum(void) {
    const char* path = temp_path("wal_bad_checksum");
    char wal_path[256];
    make_path(wal_path, sizeof(wal_path), path, "wal.log");

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "recover"));
    CHECK_OK(tsedge_append(db, "recover", 1, 11.0));

    long size = file_size_or_negative(wal_path);
    CHECK(size > 0);
    FILE* f = fopen(wal_path, "r+b");
    CHECK(f != NULL);
    CHECK(fseek(f, size - 1, SEEK_SET) == 0);
    int byte = fgetc(f);
    CHECK(byte != EOF);
    CHECK(fseek(f, size - 1, SEEK_SET) == 0);
    CHECK(fputc(byte ^ 0xff, f) != EOF);
    CHECK(fclose(f) == 0);
    db = NULL;

    tsedge_db* reopened = NULL;
    CHECK(tsedge_open(path, &reopened) == TSEDGE_ERR_CORRUPT);
    rm_rf(path);
}
