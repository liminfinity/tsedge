#include "test_helpers.h"
#include "tsedge.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void test_verify_valid_database(void) {
    const char* path = temp_path("verify_valid");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 16; ++i) {
        CHECK_OK(tsedge_append(db, "s", 1000 + i, (double)i));
    }
    CHECK_OK(tsedge_flush_all(db));

    tsedge_verify_report report;
    CHECK_OK(tsedge_verify(path, &report));
    CHECK(report.error_count == 0);
    CHECK(report.series_count >= 1);
    CHECK(report.segment_count >= 1);
    CHECK(report.block_count >= 1);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_empty_database(void) {
    const char* path = temp_path("verify_empty");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));

    tsedge_verify_report report;
    CHECK_OK(tsedge_verify(path, &report));
    CHECK(report.error_count == 0);
    CHECK(report.series_count == 0);
    CHECK(report.segment_count == 0);
    CHECK(report.block_count == 0);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_missing_metadata(void) {
    const char* path = temp_path("verify_missing_metadata");
    char metadata_path[512];
    make_series_file_path(metadata_path, sizeof(metadata_path), path, "s", "metadata.txt");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    CHECK(unlink(metadata_path) == 0);

    tsedge_verify_report report;
    CHECK(tsedge_verify(path, &report) == TSEDGE_ERR_CORRUPT);
    CHECK(report.error_count > 0);
    CHECK(strstr(report.first_error_path, "metadata.txt") != NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_invalid_segment_filename(void) {
    const char* path = temp_path("verify_bad_segment_name");
    char bad_path[512];
    make_series_file_path(bad_path, sizeof(bad_path), path, "s", "bad_segment.tse");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    FILE* f = fopen(bad_path, "wb");
    CHECK(f != NULL);
    CHECK(fclose(f) == 0);

    tsedge_verify_report report;
    CHECK(tsedge_verify(path, &report) == TSEDGE_ERR_CORRUPT);
    CHECK(report.error_count > 0);
    CHECK(strstr(report.first_error_message, "invalid segment filename") != NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_truncated_segment(void) {
    const char* path = temp_path("verify_truncated_segment");
    char segment_path[512];
    make_series_file_path(segment_path, sizeof(segment_path), path, "s", "segment_000001.tse");
    tsedge_db* db = NULL;
    CHECK_OK(tsedge_open(path, &db));
    CHECK_OK(tsedge_create_series(db, "s"));
    for (int i = 0; i < 16; ++i) {
        CHECK_OK(tsedge_append(db, "s", 1000 + i, (double)i));
    }
    CHECK_OK(tsedge_flush_all(db));

    struct stat st;
    CHECK(stat(segment_path, &st) == 0);
    CHECK(st.st_size > 8);
    CHECK(truncate(segment_path, st.st_size - 8) == 0);

    tsedge_verify_report report;
    CHECK(tsedge_verify(path, &report) == TSEDGE_ERR_CORRUPT);
    CHECK(report.error_count > 0);
    CHECK(strstr(report.first_error_message, "payload") != NULL || strstr(report.first_error_message, "truncated") != NULL);

    CHECK_OK(tsedge_close(db));
    rm_rf(path);
}

void test_verify_invalid_args(void) {
    tsedge_verify_report report;
    CHECK(tsedge_verify(NULL, &report) == TSEDGE_ERR_INVALID_ARGUMENT);
    CHECK(tsedge_verify("/tmp/tsedge_missing", NULL) == TSEDGE_ERR_INVALID_ARGUMENT);
}
