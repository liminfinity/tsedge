#define _POSIX_C_SOURCE 200809L

#include "segment_files.h"
#include "db.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int compare_u32(const void* a, const void* b) {
    uint32_t av = *(const uint32_t*)a;
    uint32_t bv = *(const uint32_t*)b;
    return (av > bv) - (av < bv);
}

int tsedge_segment_format_filename(uint32_t segment_id, char* out, size_t out_size) {
    if (!out || out_size == 0 || segment_id == 0) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int n = snprintf(out, out_size, "segment_%06u.tse", segment_id);
    return n > 0 && (size_t)n < out_size ? TSEDGE_OK : TSEDGE_ERR_INTERNAL;
}

int tsedge_segment_parse_filename(const char* name, uint32_t* out_id) {
    if (!name || !out_id) {
        return 0;
    }
    unsigned id = 0;
    char suffix = '\0';
    if (sscanf(name, "segment_%6u.tse%c", &id, &suffix) != 1 || id == 0) {
        return 0;
    }
    char expected[32];
    if (tsedge_segment_format_filename((uint32_t)id, expected, sizeof(expected)) != TSEDGE_OK) {
        return 0;
    }
    if (strcmp(name, expected) != 0) {
        return 0;
    }
    *out_id = (uint32_t)id;
    return 1;
}

char* tsedge_segment_make_path(const char* series_dir_path, uint32_t segment_id) {
    char filename[32];
    if (tsedge_segment_format_filename(segment_id, filename, sizeof(filename)) != TSEDGE_OK) {
        return NULL;
    }
    return tsedge_path_join(series_dir_path, filename);
}

uint64_t tsedge_segment_file_size_or_zero(const char* path) {
    struct stat st;
    if (!path || stat(path, &st) != 0 || st.st_size <= 0) {
        return 0;
    }
    return (uint64_t)st.st_size;
}

static int append_segment_id(uint32_t** ids, size_t* count, size_t* capacity, uint32_t segment_id) {
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 4u : *capacity * 2u;
        uint32_t* resized = (uint32_t*)realloc(*ids, next * sizeof(**ids));
        if (!resized) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        *ids = resized;
        *capacity = next;
    }
    (*ids)[(*count)++] = segment_id;
    return TSEDGE_OK;
}

int tsedge_segment_discover_ids(const char* series_dir_path, uint32_t** out_ids, size_t* out_count) {
    if (!series_dir_path || !out_ids || !out_count) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    *out_ids = NULL;
    *out_count = 0;

    DIR* dir = opendir(series_dir_path);
    if (!dir) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    uint32_t* ids = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        uint32_t segment_id = 0;
        if (!tsedge_segment_parse_filename(entry->d_name, &segment_id)) {
            continue;
        }
        char* path = tsedge_segment_make_path(series_dir_path, segment_id);
        if (!path) {
            closedir(dir);
            free(ids);
            return TSEDGE_ERR_NO_MEMORY;
        }
        struct stat st;
        int is_file = stat(path, &st) == 0 && S_ISREG(st.st_mode);
        free(path);
        if (!is_file) {
            continue;
        }
        int rc = append_segment_id(&ids, &count, &capacity, segment_id);
        if (rc != TSEDGE_OK) {
            closedir(dir);
            free(ids);
            return rc;
        }
    }

    if (closedir(dir) != 0) {
        free(ids);
        return TSEDGE_ERR_IO;
    }

    qsort(ids, count, sizeof(*ids), compare_u32);
    *out_ids = ids;
    *out_count = count;
    return TSEDGE_OK;
}
