#define _POSIX_C_SOURCE 200809L

#include "db_quota.h"
#include "series_index.h"
#include "segment_files.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    size_t series_index;
    uint32_t segment_id;
    int64_t max_timestamp;
    uint64_t size_bytes;
    char* path;
} quota_candidate;

typedef struct {
    quota_candidate* items;
    size_t count;
    size_t capacity;
} quota_candidate_list;

typedef struct {
    uint32_t segment_id;
    int64_t max_timestamp;
} quota_segment_bounds;

static int add_file_size(uint64_t* total, uint64_t size) {
    if (UINT64_MAX - *total < size) {
        return TSEDGE_ERR_INTERNAL;
    }
    *total += size;
    return TSEDGE_OK;
}

static int database_size_recursive(const char* path, uint64_t* total) {
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
        if (lstat(child, &st) != 0) {
            free(child);
            closedir(dir);
            return TSEDGE_ERR_IO;
        }

        int rc = TSEDGE_OK;
        if (S_ISDIR(st.st_mode)) {
            rc = database_size_recursive(child, total);
        } else if (S_ISREG(st.st_mode) && st.st_size > 0) {
            rc = add_file_size(total, (uint64_t)st.st_size);
        }
        free(child);
        if (rc != TSEDGE_OK) {
            closedir(dir);
            return rc;
        }
    }

    return closedir(dir) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

static int database_size_bytes(const tsedge_db* db, uint64_t* out_size) {
    if (!db || !out_size) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    *out_size = 0;
    return database_size_recursive(db->path, out_size);
}

static quota_segment_bounds* find_bounds(quota_segment_bounds* bounds, size_t count, uint32_t segment_id) {
    for (size_t i = 0; i < count; ++i) {
        if (bounds[i].segment_id == segment_id) {
            return &bounds[i];
        }
    }
    return NULL;
}

static int append_bounds(quota_segment_bounds** bounds, size_t* count, size_t* capacity, const tsedge_block_index_entry* entry) {
    quota_segment_bounds* existing = find_bounds(*bounds, *count, entry->segment_id);
    if (existing) {
        if (entry->max_timestamp > existing->max_timestamp) {
            existing->max_timestamp = entry->max_timestamp;
        }
        return TSEDGE_OK;
    }

    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 8u : *capacity * 2u;
        quota_segment_bounds* resized = (quota_segment_bounds*)realloc(*bounds, next * sizeof(**bounds));
        if (!resized) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        *bounds = resized;
        *capacity = next;
    }

    (*bounds)[*count].segment_id = entry->segment_id;
    (*bounds)[*count].max_timestamp = entry->max_timestamp;
    ++(*count);
    return TSEDGE_OK;
}

static int append_candidate(quota_candidate_list* list, size_t series_index, uint32_t segment_id, int64_t max_timestamp, const char* path, uint64_t size_bytes) {
    if (list->count == list->capacity) {
        size_t next = list->capacity == 0 ? 16u : list->capacity * 2u;
        quota_candidate* resized = (quota_candidate*)realloc(list->items, next * sizeof(*list->items));
        if (!resized) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        list->items = resized;
        list->capacity = next;
    }

    char* copy = (char*)malloc(strlen(path) + 1u);
    if (!copy) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    strcpy(copy, path);

    list->items[list->count].series_index = series_index;
    list->items[list->count].segment_id = segment_id;
    list->items[list->count].max_timestamp = max_timestamp;
    list->items[list->count].size_bytes = size_bytes;
    list->items[list->count].path = copy;
    ++list->count;
    return TSEDGE_OK;
}

static void free_candidates(quota_candidate_list* list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].path);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int collect_series_candidates(tsedge_series* series, size_t series_index, quota_candidate_list* candidates) {
    if (series->segment_count <= 1u) {
        return TSEDGE_OK;
    }

    quota_segment_bounds* bounds = NULL;
    size_t bounds_count = 0;
    size_t bounds_capacity = 0;
    for (size_t i = 0; i < series->block_index_count; ++i) {
        int rc = append_bounds(&bounds, &bounds_count, &bounds_capacity, &series->block_index[i]);
        if (rc != TSEDGE_OK) {
            free(bounds);
            return rc;
        }
    }

    for (size_t i = 0; i < bounds_count; ++i) {
        uint32_t segment_id = bounds[i].segment_id;
        if (segment_id == series->active_segment_id) {
            continue;
        }

        char* path = tsedge_segment_make_path(series->dir_path, segment_id);
        if (!path) {
            free(bounds);
            return TSEDGE_ERR_NO_MEMORY;
        }
        uint64_t size = tsedge_segment_file_size_or_zero(path);
        if (size > 0) {
            int rc = append_candidate(candidates, series_index, segment_id, bounds[i].max_timestamp, path, size);
            free(path);
            if (rc != TSEDGE_OK) {
                free(bounds);
                return rc;
            }
        } else {
            free(path);
        }
    }

    free(bounds);
    return TSEDGE_OK;
}

static int compare_candidates(const void* a, const void* b) {
    const quota_candidate* av = (const quota_candidate*)a;
    const quota_candidate* bv = (const quota_candidate*)b;
    if (av->max_timestamp < bv->max_timestamp) {
        return -1;
    }
    if (av->max_timestamp > bv->max_timestamp) {
        return 1;
    }
    if (av->series_index < bv->series_index) {
        return -1;
    }
    if (av->series_index > bv->series_index) {
        return 1;
    }
    return (av->segment_id > bv->segment_id) - (av->segment_id < bv->segment_id);
}

static int collect_quota_candidates(tsedge_db* db, quota_candidate_list* candidates) {
    for (size_t i = 0; i < db->series_count; ++i) {
        int rc = collect_series_candidates(&db->series[i], i, candidates);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    if (candidates->count > 1u) {
        qsort(candidates->items, candidates->count, sizeof(candidates->items[0]), compare_candidates);
    }
    return TSEDGE_OK;
}

static int rebuild_touched_series(tsedge_db* db, const bool* touched) {
    for (size_t i = 0; i < db->series_count; ++i) {
        if (!touched[i]) {
            continue;
        }
        int rc = tsedge_series_rebuild_index(&db->series[i]);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return TSEDGE_OK;
}

int tsedge_db_enforce_disk_quota(tsedge_db* db) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (db->disk_quota_bytes == 0) {
        db->disk_quota_exceeded = 0;
        return TSEDGE_OK;
    }

    uint64_t current_size = 0;
    int rc = database_size_bytes(db, &current_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (current_size <= db->disk_quota_bytes) {
        db->disk_quota_exceeded = 0;
        return TSEDGE_OK;
    }

    quota_candidate_list candidates;
    memset(&candidates, 0, sizeof(candidates));
    rc = collect_quota_candidates(db, &candidates);
    if (rc != TSEDGE_OK) {
        free_candidates(&candidates);
        return rc;
    }

    bool* touched = (bool*)calloc(db->series_count == 0 ? 1u : db->series_count, sizeof(*touched));
    if (!touched) {
        free_candidates(&candidates);
        return TSEDGE_ERR_NO_MEMORY;
    }

    bool deleted_any = false;
    for (size_t i = 0; i < candidates.count && current_size > db->disk_quota_bytes; ++i) {
        quota_candidate* candidate = &candidates.items[i];
        if (unlink(candidate->path) != 0 && errno != ENOENT) {
            free(touched);
            free_candidates(&candidates);
            return TSEDGE_ERR_IO;
        }
        deleted_any = true;
        touched[candidate->series_index] = true;
        current_size = current_size > candidate->size_bytes ? current_size - candidate->size_bytes : 0u;
    }

    if (deleted_any) {
        rc = rebuild_touched_series(db, touched);
        if (rc != TSEDGE_OK) {
            free(touched);
            free_candidates(&candidates);
            return rc;
        }
    }

    free(touched);
    free_candidates(&candidates);
    if (current_size <= db->disk_quota_bytes) {
        db->disk_quota_exceeded = 0;
        return TSEDGE_OK;
    }
    db->disk_quota_exceeded = 1;
    return TSEDGE_ERR_QUOTA_EXCEEDED;
}
