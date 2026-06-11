#define _POSIX_C_SOURCE 200809L

#include "db.h"
#include "tsedge.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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

static void simulate_crash(tsedge_db** db) {
    tsedge_db_free_memory(*db);
    *db = NULL;
}

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

static int count_cb_local(const tsedge_point* point, void* user_data) {
    (void)point;
    size_t* count = (size_t*)user_data;
    ++(*count);
    return 0;
}

static void rm_rf(const char* path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    int rc = system(cmd);
    (void)rc;
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

static void write_u32_le_local(unsigned char* out, uint32_t value) {
    out[0] = (unsigned char)(value & 0xffu);
    out[1] = (unsigned char)((value >> 8) & 0xffu);
    out[2] = (unsigned char)((value >> 16) & 0xffu);
    out[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void write_u64_le_local(unsigned char* out, uint64_t value) {
    for (size_t i = 0; i < 8u; ++i) {
        out[i] = (unsigned char)((value >> (i * 8u)) & 0xffu);
    }
}

static uint32_t fnv1a32_local(const unsigned char* data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
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

static int write_v2_wal_point(const char* wal_path, const char* series_name, int64_t timestamp, double value) {
    size_t name_len = strlen(series_name);
    size_t entry_size = 32u + name_len + 4u;
    unsigned char* entry = (unsigned char*)malloc(entry_size);
    if (!entry) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    write_u32_le_local(entry, 0x57455354u);
    write_u32_le_local(entry + 4, 2u);
    write_u32_le_local(entry + 8, (uint32_t)entry_size);
    write_u32_le_local(entry + 12, (uint32_t)name_len);
    write_u64_le_local(entry + 16, (uint64_t)timestamp);
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    write_u64_le_local(entry + 24, bits);
    memcpy(entry + 32, series_name, name_len);
    write_u32_le_local(entry + entry_size - 4u, fnv1a32_local(entry, entry_size - 4u));

    FILE* f = fopen(wal_path, "wb");
    if (!f) {
        free(entry);
        return TSEDGE_ERR_IO;
    }
    int ok = fwrite(entry, 1, entry_size, f) == entry_size;
    free(entry);
    return fclose(f) == 0 && ok ? TSEDGE_OK : TSEDGE_ERR_IO;
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
    simulate_crash(&db);

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
    simulate_crash(&db);

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
    simulate_crash(&db);

    tsedge_db* reopened = NULL;
    CHECK(tsedge_open(path, &reopened) == TSEDGE_ERR_CORRUPT);
    rm_rf(path);
}

void test_wal_recovery_after_batch_append(void) {
    const char* path = temp_path("batch_recovery");
    tsedge_point points[4] = {
        {1, 10.0},
        {2, 20.0},
        {3, 30.0},
        {4, 40.0},
    };

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "recover"));
    CHECK_OK(tsedge_append_batch(db, "recover", points, 4));

    /*
     * Batch append uses the same WAL-before-buffer rule as single append. By
     * skipping close, this test leaves accepted batch points only in WAL/buffer
     * state and verifies that open can replay them.
     */
    simulate_crash(&db);

    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "recover", 1, 4, collect_cb, &vec));
    CHECK(vec.count == 4);
    CHECK(vec.points[0].value == 10.0);
    CHECK(vec.points[3].value == 40.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_wal_v2_point_recovery_compatibility(void) {
    const char* path = temp_path("wal_v2_compat");
    char wal_path[256];
    make_path(wal_path, sizeof(wal_path), path, "wal.log");

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "legacy"));
    CHECK_OK(tsedge_close(db));

    CHECK_OK(write_v2_wal_point(wal_path, "legacy", 42, 24.5));

    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "legacy", 42, 42, collect_cb, &vec));
    CHECK(vec.count == 1);
    CHECK(vec.points[0].timestamp == 42);
    CHECK(vec.points[0].value == 24.5);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_wal_batch_recovery_large(void) {
    const char* path = temp_path("batch_recovery_large");
    const size_t count = 40000u;
    tsedge_point* points = (tsedge_point*)malloc(count * sizeof(*points));
    CHECK(points != NULL);
    for (size_t i = 0; i < count; ++i) {
        points[i].timestamp = 1000000 + (int64_t)i;
        points[i].value = 100.0 + (double)i * 0.01;
    }

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_set_durability(db, TSEDGE_DURABILITY_STRICT));
    CHECK_OK(tsedge_create_series(db, "recover"));
    CHECK_OK(tsedge_append_batch(db, "recover", points, count));
    simulate_crash(&db);

    CHECK_OK(tsedge_open(path, &db));
    size_t recovered = 0;
    CHECK_OK(tsedge_read_range(db, "recover", 1000000, 1000000 + (int64_t)count, count_cb_local, &recovered));
    CHECK(recovered == count);
    CHECK_OK(tsedge_close(db));
    free(points);
    rm_rf(path);
}

void test_set_durability_invalid_args(void) {
    const char* path = temp_path("durability_invalid");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));

    CHECK(tsedge_set_durability(NULL, TSEDGE_DURABILITY_FAST) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_set_durability(db, (tsedge_durability_mode)99) == TSEDGE_ERR_INVALID_ARGUMENT);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_wal_buffer_flush_on_close(void) {
    const char* path = temp_path("durability_close");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_set_durability(db, TSEDGE_DURABILITY_FAST));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 8; ++i) {
        CHECK_OK(tsedge_append(db, "s", 100 + i, 50.0 + (double)i));
    }
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 100, 107, collect_cb, &vec));
    CHECK(vec.count == 8);
    CHECK(vec.points[7].value == 57.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_wal_buffer_flush_on_manual_flush(void) {
    const char* path = temp_path("durability_manual_flush");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_set_durability(db, TSEDGE_DURABILITY_BALANCED));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 8; ++i) {
        CHECK_OK(tsedge_append(db, "s", 200 + i, 70.0 + (double)i));
    }
    CHECK_OK(tsedge_flush_all(db));
    CHECK_OK(tsedge_close(db));

    db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 200, 207, collect_cb, &vec));
    CHECK(vec.count == 8);
    CHECK(vec.points[0].value == 70.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_strict_durability_recovery(void) {
    const char* path = temp_path("durability_strict");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_set_durability(db, TSEDGE_DURABILITY_STRICT));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append(db, "s", 1, 11.0));
    CHECK_OK(tsedge_append(db, "s", 2, 12.0));
    simulate_crash(&db);

    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 1, 2, collect_cb, &vec));
    CHECK(vec.count == 2);
    CHECK(vec.points[1].value == 12.0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_fast_durability_batch_correctness(void) {
    const char* path = temp_path("durability_batch");
    tsedge_point points[4] = {
        {1, 1.0},
        {2, 2.0},
        {3, 3.0},
        {4, 4.0},
    };
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_set_durability(db, TSEDGE_DURABILITY_FAST));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append_batch(db, "s", points, 4));
    CHECK_OK(tsedge_flush_all(db));

    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 1, 4, collect_cb, &vec));
    CHECK(vec.count == 4);
    CHECK(vec.points[3].value == 4.0);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}
