#define _POSIX_C_SOURCE 200809L

#include "db.h"
#include "wal.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char* xstrdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

char* tsedge_path_join(const char* a, const char* b) {
    size_t alen = strlen(a);
    size_t blen = strlen(b);
    bool slash = alen > 0 && a[alen - 1] == '/';
    size_t len = alen + (slash ? 0u : 1u) + blen + 1u;
    char* out = (char*)malloc(len);
    if (!out) {
        return NULL;
    }
    snprintf(out, len, "%s%s%s", a, slash ? "" : "/", b);
    return out;
}

int tsedge_mkdir_if_needed(const char* path) {
    /*
     * Database open is idempotent: opening an existing directory should reuse
     * it, while opening a new path should create the minimal on-disk layout.
     */
    if (mkdir(path, 0755) == 0) {
        return TSEDGE_OK;
    }
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return TSEDGE_OK;
        }
    }
    return TSEDGE_ERR_IO;
}

static int ensure_series_capacity(tsedge_db* db) {
    if (db->series_count < db->series_capacity) {
        return TSEDGE_OK;
    }
    size_t next = db->series_capacity == 0 ? 4 : db->series_capacity * 2;
    tsedge_series* resized = (tsedge_series*)realloc(db->series, next * sizeof(*db->series));
    if (!resized) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    db->series = resized;
    db->series_capacity = next;
    return TSEDGE_OK;
}

tsedge_series* tsedge_db_find_series(tsedge_db* db, const char* name) {
    for (size_t i = 0; i < db->series_count; ++i) {
        if (strcmp(db->series[i].name, name) == 0) {
            return &db->series[i];
        }
    }
    return NULL;
}

int tsedge_db_add_series_object(tsedge_db* db, const char* name, int create_dir) {
    if (tsedge_db_find_series(db, name)) {
        return TSEDGE_OK;
    }
    int rc = ensure_series_capacity(db);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    rc = tsedge_series_init(&db->series[db->series_count], db->series_dir, name, create_dir != 0);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    ++db->series_count;
    return TSEDGE_OK;
}

int tsedge_db_rewrite_manifest(tsedge_db* db) {
    char* manifest_path = tsedge_path_join(db->path, "manifest.txt");
    if (!manifest_path) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    FILE* f = fopen(manifest_path, "w");
    free(manifest_path);
    if (!f) {
        return TSEDGE_ERR_IO;
    }
    fprintf(f, "tsedge_version=1\nseries_count=%zu\n", db->series_count);
    for (size_t i = 0; i < db->series_count; ++i) {
        fprintf(f, "series=%s\n", db->series[i].name);
    }
    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

static int load_existing_series(tsedge_db* db) {
    /*
     * Series are discovered from subdirectories rather than from a server-side
     * catalog. This keeps the prototype explainable and filesystem based.
     */
    DIR* dir = opendir(db->series_dir);
    if (!dir) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char* child = tsedge_path_join(db->series_dir, entry->d_name);
        if (!child) {
            closedir(dir);
            return TSEDGE_ERR_NO_MEMORY;
        }
        struct stat st;
        int is_dir = stat(child, &st) == 0 && S_ISDIR(st.st_mode);
        free(child);
        if (!is_dir) {
            continue;
        }

        int rc = tsedge_series_validate_name(entry->d_name);
        if (rc == TSEDGE_OK) {
            rc = tsedge_db_add_series_object(db, entry->d_name, 0);
        }
        if (rc != TSEDGE_OK) {
            closedir(dir);
            return rc;
        }
    }

    closedir(dir);
    return TSEDGE_OK;
}

void tsedge_db_free_memory(tsedge_db* db) {
    if (!db) {
        return;
    }
    for (size_t i = 0; i < db->series_count; ++i) {
        tsedge_series_free(&db->series[i]);
    }
    free(db->series);
    free(db->path);
    free(db->series_dir);
    free(db->wal_path);
    free(db);
}

int tsedge_db_flush_all(tsedge_db* db) {
    /*
     * Closing the embedded database makes buffered points durable by converting
     * each series buffer into a compressed block in its segment file.
     */
    for (size_t i = 0; i < db->series_count; ++i) {
        int rc = tsedge_series_flush(db, &db->series[i], false);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return tsedge_wal_truncate_to_buffers(db);
}

int tsedge_db_open_internal(const char* path, tsedge_db** out_db) {
    /*
     * Open builds the local directory layout, loads existing series state, and
     * then replays the WAL. There is no daemon or network protocol involved.
     */
    *out_db = NULL;
    int rc = tsedge_mkdir_if_needed(path);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    tsedge_db* db = (tsedge_db*)calloc(1, sizeof(*db));
    if (!db) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    db->path = xstrdup(path);
    db->series_dir = tsedge_path_join(path, "series");
    db->wal_path = tsedge_path_join(path, "wal.log");
    if (!db->path || !db->series_dir || !db->wal_path) {
        tsedge_db_free_memory(db);
        return TSEDGE_ERR_NO_MEMORY;
    }

    rc = tsedge_mkdir_if_needed(db->series_dir);
    if (rc == TSEDGE_OK) {
        rc = load_existing_series(db);
    }
    if (rc == TSEDGE_OK) {
        /* WAL replay restores points that were accepted but not flushed. */
        rc = tsedge_wal_replay(db);
    }
    if (rc == TSEDGE_OK) {
        rc = tsedge_db_rewrite_manifest(db);
    }
    if (rc != TSEDGE_OK) {
        tsedge_db_free_memory(db);
        return rc;
    }

    *out_db = db;
    return TSEDGE_OK;
}

int tsedge_db_close_internal(tsedge_db* db) {
    if (!db) {
        return TSEDGE_OK;
    }
    int rc = tsedge_db_flush_all(db);
    tsedge_db_free_memory(db);
    return rc;
}

int tsedge_db_create_series_internal(tsedge_db* db, const char* name) {
    if (tsedge_db_find_series(db, name)) {
        return TSEDGE_OK;
    }
    int rc = tsedge_db_add_series_object(db, name, 1);
    if (rc == TSEDGE_OK) {
        rc = tsedge_db_rewrite_manifest(db);
    }
    return rc;
}
