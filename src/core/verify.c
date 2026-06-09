#define _POSIX_C_SOURCE 200809L

#include "verify.h"
#include "block.h"
#include "db.h"
#include "segment_files.h"
#include "wal.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int set_error(tsedge_verify_report* report, const char* path, const char* message, int status) {
    if (report->error_count == 0) {
        snprintf(report->first_error_path, sizeof(report->first_error_path), "%s", path ? path : "");
        snprintf(report->first_error_message, sizeof(report->first_error_message), "%s", message ? message : "verification failed");
    }
    ++report->error_count;
    return status;
}

static bool is_dot_entry(const char* name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static bool has_suffix(const char* name, const char* suffix) {
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    return name_len >= suffix_len && strcmp(name + name_len - suffix_len, suffix) == 0;
}

static int stat_path(tsedge_verify_report* report, const char* path, struct stat* st, const char* message) {
    if (stat(path, st) != 0) {
        return set_error(report, path, message, errno == ENOENT ? TSEDGE_ERR_CORRUPT : TSEDGE_ERR_IO);
    }
    return TSEDGE_OK;
}

static int verify_manifest(const char* db_path, tsedge_verify_report* report) {
    char* path = tsedge_path_join(db_path, "manifest.txt");
    if (!path) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        int rc = set_error(report, path, "manifest.txt is missing", errno == ENOENT ? TSEDGE_ERR_CORRUPT : TSEDGE_ERR_IO);
        free(path);
        return rc;
    }

    char line[128];
    if (!fgets(line, sizeof(line), f) || strncmp(line, "tsedge_version=", 15) != 0) {
        fclose(f);
        int rc = set_error(report, path, "invalid manifest header", TSEDGE_ERR_CORRUPT);
        free(path);
        return rc;
    }

    if (fclose(f) != 0) {
        int rc = set_error(report, path, "manifest.txt is not readable", TSEDGE_ERR_IO);
        free(path);
        return rc;
    }
    free(path);
    return TSEDGE_OK;
}

static int verify_metadata(const char* series_dir, tsedge_verify_report* report) {
    char* path = tsedge_path_join(series_dir, "metadata.txt");
    if (!path) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        int rc = set_error(report, path, "metadata.txt is missing", errno == ENOENT ? TSEDGE_ERR_CORRUPT : TSEDGE_ERR_IO);
        free(path);
        return rc;
    }

    char line[256];
    bool has_name = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "name=", 5) == 0 && line[5] != '\0' && line[5] != '\n') {
            has_name = true;
            break;
        }
    }
    if (ferror(f)) {
        fclose(f);
        int rc = set_error(report, path, "metadata.txt is not readable", TSEDGE_ERR_IO);
        free(path);
        return rc;
    }
    if (!has_name) {
        fclose(f);
        int rc = set_error(report, path, "metadata.txt has no series name", TSEDGE_ERR_CORRUPT);
        free(path);
        return rc;
    }
    if (fclose(f) != 0) {
        int rc = set_error(report, path, "metadata.txt is not readable", TSEDGE_ERR_IO);
        free(path);
        return rc;
    }
    free(path);
    return TSEDGE_OK;
}

static int verify_segment_file(const char* path, tsedge_verify_report* report) {
    struct stat st;
    int rc = stat_path(report, path, &st, "segment file is not readable");
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (!S_ISREG(st.st_mode)) {
        return set_error(report, path, "segment path is not a regular file", TSEDGE_ERR_CORRUPT);
    }
    if (st.st_size <= 0) {
        return set_error(report, path, "segment file is empty", TSEDGE_ERR_CORRUPT);
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        return set_error(report, path, "segment file is not readable", TSEDGE_ERR_IO);
    }

    for (;;) {
        long offset = ftell(f);
        if (offset < 0) {
            fclose(f);
            return set_error(report, path, "segment offset is invalid", TSEDGE_ERR_IO);
        }
        if ((off_t)offset == st.st_size) {
            break;
        }
        if (st.st_size - (off_t)offset < (off_t)TSEDGE_BLOCK_HEADER_SIZE) {
            fclose(f);
            return set_error(report, path, "block header is truncated", TSEDGE_ERR_CORRUPT);
        }

        tsedge_block_header header;
        bool eof = false;
        rc = tsedge_block_read_header(f, &header, &eof);
        if (rc != TSEDGE_OK || eof) {
            fclose(f);
            return set_error(report, path, "invalid block header", TSEDGE_ERR_CORRUPT);
        }

        off_t block_end = (off_t)offset + (off_t)TSEDGE_BLOCK_HEADER_SIZE + (off_t)header.payload_size;
        if (block_end > st.st_size) {
            fclose(f);
            return set_error(report, path, "block payload exceeds file size", TSEDGE_ERR_CORRUPT);
        }
        if (fseek(f, (long)header.payload_size, SEEK_CUR) != 0) {
            fclose(f);
            return set_error(report, path, "block payload is not readable", TSEDGE_ERR_IO);
        }
        ++report->block_count;
    }

    if (fclose(f) != 0) {
        return set_error(report, path, "segment file is not readable", TSEDGE_ERR_IO);
    }
    ++report->segment_count;
    return TSEDGE_OK;
}

static int verify_series_dir(const char* series_dir, tsedge_verify_report* report) {
    int rc = verify_metadata(series_dir, report);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    DIR* dir = opendir(series_dir);
    if (!dir) {
        return set_error(report, series_dir, "series directory is not readable", TSEDGE_ERR_IO);
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (is_dot_entry(entry->d_name)) {
            continue;
        }
        if (strcmp(entry->d_name, "metadata.txt") == 0) {
            continue;
        }

        char* path = tsedge_path_join(series_dir, entry->d_name);
        if (!path) {
            closedir(dir);
            return TSEDGE_ERR_NO_MEMORY;
        }

        uint32_t segment_id = 0;
        if (has_suffix(entry->d_name, ".tse") && !tsedge_segment_parse_filename(entry->d_name, &segment_id)) {
            rc = set_error(report, path, "invalid segment filename", TSEDGE_ERR_CORRUPT);
            free(path);
            closedir(dir);
            return rc;
        }
        if (segment_id != 0) {
            rc = verify_segment_file(path, report);
            free(path);
            if (rc != TSEDGE_OK) {
                closedir(dir);
                return rc;
            }
            continue;
        }

        free(path);
    }

    if (closedir(dir) != 0) {
        return set_error(report, series_dir, "series directory is not readable", TSEDGE_ERR_IO);
    }
    ++report->series_count;
    return TSEDGE_OK;
}

static int verify_series_root(const char* db_path, tsedge_verify_report* report) {
    char* series_root = tsedge_path_join(db_path, "series");
    if (!series_root) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    struct stat st;
    int rc = stat_path(report, series_root, &st, "series directory is missing");
    if (rc != TSEDGE_OK) {
        free(series_root);
        return rc;
    }
    if (!S_ISDIR(st.st_mode)) {
        rc = set_error(report, series_root, "series path is not a directory", TSEDGE_ERR_CORRUPT);
        free(series_root);
        return rc;
    }

    DIR* dir = opendir(series_root);
    if (!dir) {
        rc = set_error(report, series_root, "series directory is not readable", TSEDGE_ERR_IO);
        free(series_root);
        return rc;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (is_dot_entry(entry->d_name) || entry->d_name[0] == '.') {
            continue;
        }
        char* path = tsedge_path_join(series_root, entry->d_name);
        if (!path) {
            closedir(dir);
            free(series_root);
            return TSEDGE_ERR_NO_MEMORY;
        }

        rc = stat_path(report, path, &st, "series entry is not readable");
        if (rc != TSEDGE_OK) {
            free(path);
            closedir(dir);
            free(series_root);
            return rc;
        }
        if (!S_ISDIR(st.st_mode)) {
            rc = set_error(report, path, "series entry is not a directory", TSEDGE_ERR_CORRUPT);
            free(path);
            closedir(dir);
            free(series_root);
            return rc;
        }

        rc = verify_series_dir(path, report);
        free(path);
        if (rc != TSEDGE_OK) {
            closedir(dir);
            free(series_root);
            return rc;
        }
    }

    if (closedir(dir) != 0) {
        rc = set_error(report, series_root, "series directory is not readable", TSEDGE_ERR_IO);
        free(series_root);
        return rc;
    }
    free(series_root);
    return TSEDGE_OK;
}

static int verify_wal(const char* db_path, tsedge_verify_report* report) {
    char* path = tsedge_path_join(db_path, "wal.log");
    if (!path) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    size_t count = 0;
    int rc = tsedge_wal_verify_file(path, &count);
    if (rc == TSEDGE_ERR_CORRUPT) {
        rc = set_error(report, path, "wal entry is corrupted", rc);
    } else if (rc != TSEDGE_OK) {
        rc = set_error(report, path, "wal.log is not readable", rc);
    } else {
        report->wal_entry_count = count;
    }
    free(path);
    return rc;
}

int tsedge_verify_internal(const char* db_path, tsedge_verify_report* report) {
    if (!db_path || !report) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    memset(report, 0, sizeof(*report));

    struct stat st;
    int rc = stat_path(report, db_path, &st, "database path does not exist");
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (!S_ISDIR(st.st_mode)) {
        return set_error(report, db_path, "database path is not a directory", TSEDGE_ERR_CORRUPT);
    }

    rc = verify_manifest(db_path, report);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = verify_series_root(db_path, report);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = verify_wal(db_path, report);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return report->error_count == 0 ? TSEDGE_OK : TSEDGE_ERR_CORRUPT;
}
