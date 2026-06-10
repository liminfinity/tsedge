#define _POSIX_C_SOURCE 200809L

#include "series_delete.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int remove_directory_recursive(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char* child = tsedge_path_join(path, entry->d_name);
        if (!child) {
            closedir(dir);
            return TSEDGE_ERR_NO_MEMORY;
        }

        struct stat st;
        int rc = TSEDGE_OK;
        if (lstat(child, &st) != 0) {
            rc = TSEDGE_ERR_IO;
        } else if (S_ISDIR(st.st_mode)) {
            rc = remove_directory_recursive(child);
        } else if (unlink(child) != 0) {
            rc = TSEDGE_ERR_IO;
        }
        free(child);
        if (rc != TSEDGE_OK) {
            closedir(dir);
            return rc;
        }
    }

    if (closedir(dir) != 0) {
        return TSEDGE_ERR_IO;
    }
    return rmdir(path) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

static size_t series_index_by_name(const tsedge_db* db, const char* series_name, int* found) {
    *found = 0;
    for (size_t i = 0; i < db->series_count; ++i) {
        if (strcmp(db->series[i].name, series_name) == 0) {
            *found = 1;
            return i;
        }
    }
    return 0;
}

int tsedge_db_delete_series(tsedge_db* db, const char* series_name) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = tsedge_series_validate_name(series_name);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    int found = 0;
    size_t index = series_index_by_name(db, series_name, &found);
    if (!found) {
        return TSEDGE_ERR_NOT_FOUND;
    }

    /*
     * Flush all buffers first. This reuses the normal WAL truncation path, so
     * no pending WAL entry can resurrect the series after it is removed.
     */
    rc = tsedge_db_flush_all(db);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    rc = remove_directory_recursive(db->series[index].dir_path);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    tsedge_series_free(&db->series[index]);
    if (index + 1u < db->series_count) {
        memmove(
            &db->series[index],
            &db->series[index + 1u],
            (db->series_count - index - 1u) * sizeof(db->series[0])
        );
    }
    --db->series_count;
    memset(&db->series[db->series_count], 0, sizeof(db->series[0]));

    return tsedge_db_rewrite_manifest(db);
}
