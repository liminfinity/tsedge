#define _POSIX_C_SOURCE 200809L

#include "db.h"
#include "test_helpers.h"
#include "tsedge.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BLOCK_POINT_COUNT_OFFSET 8L
#define BLOCK_MIN_TIMESTAMP_OFFSET 16L
#define BLOCK_MAX_TIMESTAMP_OFFSET 24L
#define BLOCK_TIMESTAMP_SIZE_OFFSET 32L
#define BLOCK_PAYLOAD_SIZE_OFFSET 40L

static void write_u32_le_local(unsigned char* out, uint32_t value) {
    out[0] = (unsigned char)(value & 0xffu);
    out[1] = (unsigned char)((value >> 8) & 0xffu);
    out[2] = (unsigned char)((value >> 16) & 0xffu);
    out[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void write_i64_le_local(unsigned char* out, int64_t value) {
    uint64_t raw = (uint64_t)value;
    for (size_t i = 0; i < 8u; ++i) {
        out[i] = (unsigned char)((raw >> (i * 8u)) & 0xffu);
    }
}

static int overwrite_bytes(const char* path, long offset, const void* data, size_t size) {
    FILE* f = fopen(path, "r+b");
    if (!f) {
        return -1;
    }
    int ok = fseek(f, offset, SEEK_SET) == 0 && fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int overwrite_u32(const char* path, long offset, uint32_t value) {
    unsigned char buf[4];
    write_u32_le_local(buf, value);
    return overwrite_bytes(path, offset, buf, sizeof(buf));
}

static int overwrite_i64(const char* path, long offset, int64_t value) {
    unsigned char buf[8];
    write_i64_le_local(buf, value);
    return overwrite_bytes(path, offset, buf, sizeof(buf));
}

static int truncate_file_to(const char* path, long size) {
    return truncate(path, (off_t)size);
}

static int file_size(const char* path, long* out_size) {
    struct stat st;
    if (!out_size || stat(path, &st) != 0) {
        return -1;
    }
    *out_size = (long)st.st_size;
    return 0;
}

static uint32_t read_wal_entry_size(const char* wal_path) {
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
    return (uint32_t)prefix[8] |
        ((uint32_t)prefix[9] << 8) |
        ((uint32_t)prefix[10] << 16) |
        ((uint32_t)prefix[11] << 24);
}

static int create_flushed_series(const char* path, const char* series_name, size_t count) {
    tsedge_db* db = NULL;
    int rc = tsedge_open(path, &db);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = tsedge_create_series(db, series_name);
    if (rc != TSEDGE_OK) {
        tsedge_close(db);
        return rc;
    }
    for (size_t i = 0; i < count; ++i) {
        rc = tsedge_append(db, series_name, 1000 + (int64_t)i, 10.0 + (double)i);
        if (rc != TSEDGE_OK) {
            tsedge_close(db);
            return rc;
        }
    }
    rc = tsedge_flush_all(db);
    if (rc == TSEDGE_OK) {
        rc = tsedge_close(db);
    } else {
        tsedge_close(db);
    }
    return rc;
}

static void segment_path_for(char* out, size_t out_size, const char* path) {
    make_series_file_path(out, out_size, path, "s", "segment_000001.tse");
}

static uint32_t fuzz_next(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static void simulate_crash(tsedge_db** db) {
    tsedge_db_free_memory(*db);
    *db = NULL;
}

static int create_fake_segment_database(const char* path, const unsigned char* data, size_t size) {
    tsedge_db* db = NULL;
    int rc = tsedge_open(path, &db);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = tsedge_create_series(db, "s");
    if (rc == TSEDGE_OK) {
        rc = tsedge_close(db);
    } else {
        tsedge_close(db);
    }
    if (rc != TSEDGE_OK) {
        return rc;
    }

    char segment_path[512];
    segment_path_for(segment_path, sizeof(segment_path), path);
    FILE* f = fopen(segment_path, "wb");
    if (!f) {
        return TSEDGE_ERR_IO;
    }
    if (size > 0 && fwrite(data, 1, size, f) != size) {
        fclose(f);
        return TSEDGE_ERR_IO;
    }
    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

static void expect_verify_corrupt(const char* path) {
    tsedge_verify_report report;
    int rc = tsedge_verify(path, &report);
    CHECK(rc == TSEDGE_ERR_CORRUPT);
    CHECK(report.error_count > 0);
    CHECK(report.first_error_message[0] != '\0');
}

void test_corrupt_segment_magic(void) {
    const char* path = temp_path("corrupt_segment_magic");
    char segment_path[512];
    segment_path_for(segment_path, sizeof(segment_path), path);
    CHECK_OK(create_flushed_series(path, "s", 32));

    unsigned char bad_magic[4] = {0xde, 0xad, 0xbe, 0xef};
    CHECK(overwrite_bytes(segment_path, 0, bad_magic, sizeof(bad_magic)) == 0);
    expect_verify_corrupt(path);
    rm_rf(path);
}

void test_corrupt_segment_truncated(void) {
    const char* path = temp_path("corrupt_segment_truncated");
    char segment_path[512];
    long size = 0;
    segment_path_for(segment_path, sizeof(segment_path), path);
    CHECK_OK(create_flushed_series(path, "s", 32));
    CHECK(file_size(segment_path, &size) == 0);
    CHECK(size > 16);
    CHECK(truncate_file_to(segment_path, size - 8) == 0);

    expect_verify_corrupt(path);
    rm_rf(path);
}

void test_corrupt_segment_payload_size(void) {
    const char* path = temp_path("corrupt_segment_payload");
    char segment_path[512];
    segment_path_for(segment_path, sizeof(segment_path), path);
    CHECK_OK(create_flushed_series(path, "s", 32));
    CHECK(overwrite_u32(segment_path, BLOCK_PAYLOAD_SIZE_OFFSET, 0x7fffffffu) == 0);

    expect_verify_corrupt(path);
    rm_rf(path);
}

void test_corrupt_segment_point_count_zero(void) {
    const char* path = temp_path("corrupt_segment_point_zero");
    char segment_path[512];
    segment_path_for(segment_path, sizeof(segment_path), path);
    CHECK_OK(create_flushed_series(path, "s", 32));
    CHECK(overwrite_u32(segment_path, BLOCK_POINT_COUNT_OFFSET, 0u) == 0);

    expect_verify_corrupt(path);
    rm_rf(path);
}

void test_corrupt_segment_point_count_huge(void) {
    const char* path = temp_path("corrupt_segment_point_huge");
    char segment_path[512];
    segment_path_for(segment_path, sizeof(segment_path), path);
    CHECK_OK(create_flushed_series(path, "s", 32));
    CHECK(overwrite_u32(segment_path, BLOCK_POINT_COUNT_OFFSET, 0xffffffffu) == 0);

    expect_verify_corrupt(path);
    rm_rf(path);
}

void test_corrupt_segment_timestamp_range(void) {
    const char* path = temp_path("corrupt_segment_timestamp_range");
    char segment_path[512];
    segment_path_for(segment_path, sizeof(segment_path), path);
    CHECK_OK(create_flushed_series(path, "s", 32));
    CHECK(overwrite_i64(segment_path, BLOCK_MIN_TIMESTAMP_OFFSET, 2000) == 0);
    CHECK(overwrite_i64(segment_path, BLOCK_MAX_TIMESTAMP_OFFSET, 1000) == 0);

    expect_verify_corrupt(path);
    rm_rf(path);
}

void test_corrupt_segment_read_range_returns_error(void) {
    const char* path = temp_path("corrupt_segment_read_range");
    char segment_path[512];
    segment_path_for(segment_path, sizeof(segment_path), path);
    CHECK_OK(create_flushed_series(path, "s", 32));
    CHECK(overwrite_u32(segment_path, BLOCK_TIMESTAMP_SIZE_OFFSET, 0x7fffffffu) == 0);

    tsedge_db* db = NULL;
    int rc = tsedge_open(path, &db);
    if (rc == TSEDGE_OK) {
        point_vec vec;
        memset(&vec, 0, sizeof(vec));
        rc = tsedge_read_range(db, "s", 1000, 2000, collect_cb, &vec);
        CHECK(rc != TSEDGE_OK);
        CHECK(vec.count == 0);
        CHECK_OK(tsedge_close(db));
    } else {
        CHECK(rc == TSEDGE_ERR_CORRUPT);
    }
    rm_rf(path);
}

void test_corrupt_wal_checksum(void) {
    const char* path = temp_path("corrupt_wal_checksum");
    char wal_path[512];
    snprintf(wal_path, sizeof(wal_path), "%s/wal.log", path);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append(db, "s", 1, 11.0));

    long size = 0;
    CHECK(file_size(wal_path, &size) == 0);
    CHECK(size > 0);
    unsigned char bad = 0;
    FILE* f = fopen(wal_path, "r+b");
    CHECK(f != NULL);
    CHECK(fseek(f, size - 1, SEEK_SET) == 0);
    int byte = fgetc(f);
    CHECK(byte != EOF);
    bad = (unsigned char)((unsigned char)byte ^ 0xffu);
    CHECK(fseek(f, size - 1, SEEK_SET) == 0);
    CHECK(fwrite(&bad, 1, 1, f) == 1);
    CHECK(fclose(f) == 0);
    simulate_crash(&db);

    CHECK(tsedge_open(path, &db) == TSEDGE_ERR_CORRUPT);
    rm_rf(path);
}

void test_corrupt_wal_torn_final_entry(void) {
    const char* path = temp_path("corrupt_wal_torn_final");
    char wal_path[512];
    snprintf(wal_path, sizeof(wal_path), "%s/wal.log", path);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK_OK(tsedge_append(db, "s", 1, 11.0));
    CHECK_OK(tsedge_append(db, "s", 2, 12.0));

    uint32_t first_size = read_wal_entry_size(wal_path);
    CHECK(first_size > 0);
    CHECK(truncate_file_to(wal_path, (long)first_size + 8) == 0);
    simulate_crash(&db);

    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 1, 2, collect_cb, &vec));
    CHECK(vec.count == 1);
    CHECK(vec.points[0].timestamp == 1);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

static int create_batch_wal_database(const char* path, size_t count) {
    tsedge_point* points = make_linear_points(count, 5000);
    if (!points) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    tsedge_db* db = NULL;
    int rc = tsedge_open(path, &db);
    if (rc != TSEDGE_OK) {
        free(points);
        return rc;
    }
    rc = tsedge_set_durability(db, TSEDGE_DURABILITY_STRICT);
    if (rc == TSEDGE_OK) {
        rc = tsedge_create_series(db, "s");
    }
    if (rc == TSEDGE_OK) {
        rc = tsedge_append_batch(db, "s", points, count);
    }
    free(points);
    if (rc != TSEDGE_OK) {
        tsedge_close(db);
        return rc;
    }
    simulate_crash(&db);
    return TSEDGE_OK;
}

void test_corrupt_wal_batch_checksum(void) {
    const char* path = temp_path("corrupt_wal_batch_checksum");
    char wal_path[512];
    long size = 0;
    snprintf(wal_path, sizeof(wal_path), "%s/wal.log", path);
    CHECK_OK(create_batch_wal_database(path, 16));
    CHECK(file_size(wal_path, &size) == 0);
    CHECK(size > 0);

    FILE* f = fopen(wal_path, "r+b");
    CHECK(f != NULL);
    CHECK(fseek(f, size - 1, SEEK_SET) == 0);
    int byte = fgetc(f);
    CHECK(byte != EOF);
    unsigned char bad = (unsigned char)((unsigned char)byte ^ 0xffu);
    CHECK(fseek(f, size - 1, SEEK_SET) == 0);
    CHECK(fwrite(&bad, 1, 1, f) == 1);
    CHECK(fclose(f) == 0);

    tsedge_db* db = NULL;
    CHECK(tsedge_open(path, &db) == TSEDGE_ERR_CORRUPT);
    rm_rf(path);
}

void test_corrupt_wal_batch_torn_final_entry(void) {
    const char* path = temp_path("corrupt_wal_batch_torn");
    char wal_path[512];
    long size = 0;
    snprintf(wal_path, sizeof(wal_path), "%s/wal.log", path);
    CHECK_OK(create_batch_wal_database(path, 16));
    CHECK(file_size(wal_path, &size) == 0);
    CHECK(size > 16);
    CHECK(truncate_file_to(wal_path, size - 8) == 0);

    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    point_vec vec;
    memset(&vec, 0, sizeof(vec));
    CHECK_OK(tsedge_read_range(db, "s", 5000, 6000, collect_cb, &vec));
    CHECK(vec.count == 0);
    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_corrupt_wal_batch_invalid_record_type(void) {
    const char* path = temp_path("corrupt_wal_batch_type");
    char wal_path[512];
    snprintf(wal_path, sizeof(wal_path), "%s/wal.log", path);
    CHECK_OK(create_batch_wal_database(path, 16));
    CHECK(overwrite_u32(wal_path, 12L, 99u) == 0);

    tsedge_db* db = NULL;
    CHECK(tsedge_open(path, &db) == TSEDGE_ERR_CORRUPT);
    rm_rf(path);
}

void test_corrupt_wal_batch_invalid_point_count(void) {
    const char* path = temp_path("corrupt_wal_batch_count");
    char wal_path[512];
    snprintf(wal_path, sizeof(wal_path), "%s/wal.log", path);
    CHECK_OK(create_batch_wal_database(path, 16));
    CHECK(overwrite_u32(wal_path, 20L, 0xffffffffu) == 0);

    tsedge_db* db = NULL;
    CHECK(tsedge_open(path, &db) == TSEDGE_ERR_CORRUPT);
    rm_rf(path);
}

void test_random_bytes_segment_files(void) {
    static const size_t sizes[] = {0u, 1u, 4u, 16u, 64u, 256u};
    uint32_t seed = 0xC0FFEEu;

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        const char* path = temp_path("random_segment_bytes");
        unsigned char data[256];
        for (size_t j = 0; j < sizes[i]; ++j) {
            data[j] = (unsigned char)(fuzz_next(&seed) & 0xffu);
        }
        CHECK_OK(create_fake_segment_database(path, data, sizes[i]));

        tsedge_verify_report report;
        int rc = tsedge_verify(path, &report);
        CHECK(rc == TSEDGE_OK || rc == TSEDGE_ERR_CORRUPT);
        if (rc != TSEDGE_OK) {
            CHECK(report.error_count > 0);
            CHECK(report.first_error_message[0] != '\0');
        }
        rm_rf(path);
    }
}

void test_fuzz_like_random_segment_inputs(void) {
    uint32_t seed = 0xC0FFEEu;
    unsigned char data[512];

    for (size_t i = 0; i < 128u; ++i) {
        const char* path = temp_path("fuzz_segment");
        size_t size = (i * 37u) % sizeof(data);
        for (size_t j = 0; j < size; ++j) {
            data[j] = (unsigned char)(fuzz_next(&seed) & 0xffu);
        }
        CHECK_OK(create_fake_segment_database(path, data, size));

        tsedge_verify_report report;
        int rc = tsedge_verify(path, &report);
        CHECK(rc == TSEDGE_OK || rc == TSEDGE_ERR_CORRUPT);
        if (rc != TSEDGE_OK) {
            CHECK(report.error_count > 0);
        }
        rm_rf(path);
    }
}
