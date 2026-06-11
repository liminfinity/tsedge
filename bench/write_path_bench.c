#define _POSIX_C_SOURCE 200809L

#include "tsedge.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    MODE_APPEND,
    MODE_APPEND_HANDLE,
    MODE_BATCH,
    MODE_BATCH_HANDLE
} write_mode;

typedef struct {
    size_t points;
    size_t batch_size;
    size_t series_count;
    write_mode mode;
    tsedge_durability_mode durability;
    const char* db_path;
} bench_config;

typedef struct {
    uint64_t compressed_size_bytes;
    uint64_t raw_size_estimate_bytes;
    uint64_t segment_count;
    uint64_t block_count;
    uint64_t indexed_points;
} storage_totals;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void print_help(const char* program) {
    printf("Usage: %s [options]\n", program);
    printf("Options:\n");
    printf("  --points N       Total points to write (default: 100000)\n");
    printf("  --batch-size N   Points per tsedge_append_batch call (default: 1000)\n");
    printf("  --series N       Number of series (default: 1)\n");
    printf("  --mode append          Write through tsedge_append\n");
    printf("  --mode append_handle   Write through tsedge_append_handle\n");
    printf("  --mode batch           Write through tsedge_append_batch (default)\n");
    printf("  --mode batch_handle    Write through tsedge_append_batch_handle\n");
    printf("  --durability fast      Buffered WAL, highest throughput\n");
    printf("  --durability balanced  Buffered WAL, smaller flush threshold\n");
    printf("  --durability strict    Flush WAL on every append or batch (default)\n");
    printf("  --db-path PATH   Benchmark database path (default: write_bench_db)\n");
    printf("  --help           Show this help\n");
}

static int parse_size(const char* text, size_t* out) {
    char* end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0) {
        return 0;
    }
    *out = (size_t)value;
    return 1;
}

static int parse_args(int argc, char** argv, bench_config* config) {
    config->points = 100000u;
    config->batch_size = 1000u;
    config->series_count = 1u;
    config->mode = MODE_BATCH;
    config->durability = TSEDGE_DURABILITY_STRICT;
    config->db_path = "write_bench_db";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--points") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->points)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--batch-size") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->batch_size)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--series") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->series_count)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (strcmp(mode, "append") == 0) {
                config->mode = MODE_APPEND;
            } else if (strcmp(mode, "append_handle") == 0) {
                config->mode = MODE_APPEND_HANDLE;
            } else if (strcmp(mode, "batch") == 0) {
                config->mode = MODE_BATCH;
            } else if (strcmp(mode, "batch_handle") == 0) {
                config->mode = MODE_BATCH_HANDLE;
            } else {
                return 0;
            }
        } else if (strcmp(argv[i], "--durability") == 0 && i + 1 < argc) {
            const char* durability = argv[++i];
            if (strcmp(durability, "fast") == 0) {
                config->durability = TSEDGE_DURABILITY_FAST;
            } else if (strcmp(durability, "balanced") == 0) {
                config->durability = TSEDGE_DURABILITY_BALANCED;
            } else if (strcmp(durability, "strict") == 0) {
                config->durability = TSEDGE_DURABILITY_STRICT;
            } else {
                return 0;
            }
        } else if (strcmp(argv[i], "--db-path") == 0 && i + 1 < argc) {
            config->db_path = argv[++i];
        } else {
            return 0;
        }
    }

    return config->points > 0 && config->series_count > 0 && config->batch_size > 0;
}

static int remove_tree(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        return errno == ENOENT ? 0 : -1;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child[1024];
        int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(child)) {
            closedir(dir);
            return -1;
        }

        struct stat st;
        if (lstat(child, &st) != 0) {
            closedir(dir);
            return -1;
        }
        if (S_ISDIR(st.st_mode)) {
            if (remove_tree(child) != 0) {
                closedir(dir);
                return -1;
            }
        } else if (unlink(child) != 0) {
            closedir(dir);
            return -1;
        }
    }

    if (closedir(dir) != 0) {
        return -1;
    }
    return rmdir(path);
}

static uint64_t directory_size(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        return (uint64_t)st.st_size;
    }

    DIR* dir = opendir(path);
    if (!dir) {
        return 0;
    }

    uint64_t total = 0;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[1024];
        int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (n >= 0 && (size_t)n < sizeof(child)) {
            total += directory_size(child);
        }
    }
    closedir(dir);
    return total;
}

static uint64_t file_size_bytes(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return (uint64_t)st.st_size;
}

static int make_series_name(size_t index, char* out, size_t out_size) {
    int n = snprintf(out, out_size, "bench.series_%02zu", index);
    return n >= 0 && (size_t)n < out_size;
}

static int64_t sample_timestamp(size_t local_index) {
    return 1710000000000LL + (int64_t)local_index * 1000LL;
}

static double sample_value(size_t series_index, size_t local_index) {
    return 20.0 + (double)series_index * 0.25 + (double)(local_index % 100u) * 0.01;
}

static int create_series(tsedge_db* db, char (*names)[64], size_t series_count) {
    for (size_t i = 0; i < series_count; ++i) {
        if (!make_series_name(i, names[i], 64u)) {
            return TSEDGE_ERR_INVALID_ARGUMENT;
        }
        int rc = tsedge_create_series(db, names[i]);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return TSEDGE_OK;
}

static int get_handles(tsedge_db* db, char (*names)[64], tsedge_series_handle** handles, size_t series_count) {
    for (size_t i = 0; i < series_count; ++i) {
        int rc = tsedge_get_series_handle(db, names[i], &handles[i]);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return TSEDGE_OK;
}

static int write_append(tsedge_db* db, char (*names)[64], const bench_config* config) {
    for (size_t i = 0; i < config->points; ++i) {
        size_t series_index = i % config->series_count;
        size_t local_index = i / config->series_count;
        int rc = tsedge_append(
            db,
            names[series_index],
            sample_timestamp(local_index),
            sample_value(series_index, local_index)
        );
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return TSEDGE_OK;
}

static int write_append_handle(tsedge_db* db, tsedge_series_handle** handles, const bench_config* config) {
    for (size_t i = 0; i < config->points; ++i) {
        size_t series_index = i % config->series_count;
        size_t local_index = i / config->series_count;
        int rc = tsedge_append_handle(
            db,
            handles[series_index],
            sample_timestamp(local_index),
            sample_value(series_index, local_index)
        );
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return TSEDGE_OK;
}

static size_t points_for_series(const bench_config* config, size_t series_index) {
    size_t base = config->points / config->series_count;
    size_t remainder = config->points % config->series_count;
    return base + (series_index < remainder ? 1u : 0u);
}

static int write_batch(tsedge_db* db, char (*names)[64], const bench_config* config) {
    tsedge_point* batch = (tsedge_point*)malloc(config->batch_size * sizeof(*batch));
    if (!batch) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    for (size_t series_index = 0; series_index < config->series_count; ++series_index) {
        size_t series_points = points_for_series(config, series_index);
        for (size_t i = 0; i < series_points;) {
            size_t chunk = series_points - i;
            if (chunk > config->batch_size) {
                chunk = config->batch_size;
            }
            for (size_t j = 0; j < chunk; ++j) {
                size_t local_index = i + j;
                batch[j].timestamp = sample_timestamp(local_index);
                batch[j].value = sample_value(series_index, local_index);
            }
            int rc = tsedge_append_batch(db, names[series_index], batch, chunk);
            if (rc != TSEDGE_OK) {
                free(batch);
                return rc;
            }
            i += chunk;
        }
    }

    free(batch);
    return TSEDGE_OK;
}

static int write_batch_handle(tsedge_db* db, tsedge_series_handle** handles, const bench_config* config) {
    tsedge_point* batch = (tsedge_point*)malloc(config->batch_size * sizeof(*batch));
    if (!batch) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    for (size_t series_index = 0; series_index < config->series_count; ++series_index) {
        size_t series_points = points_for_series(config, series_index);
        for (size_t i = 0; i < series_points;) {
            size_t chunk = series_points - i;
            if (chunk > config->batch_size) {
                chunk = config->batch_size;
            }
            for (size_t j = 0; j < chunk; ++j) {
                size_t local_index = i + j;
                batch[j].timestamp = sample_timestamp(local_index);
                batch[j].value = sample_value(series_index, local_index);
            }
            int rc = tsedge_append_batch_handle(db, handles[series_index], batch, chunk);
            if (rc != TSEDGE_OK) {
                free(batch);
                return rc;
            }
            i += chunk;
        }
    }

    free(batch);
    return TSEDGE_OK;
}

static int collect_storage_totals(tsedge_db* db, char (*names)[64], size_t series_count, storage_totals* totals) {
    memset(totals, 0, sizeof(*totals));
    for (size_t i = 0; i < series_count; ++i) {
        tsedge_series_stats stats;
        int rc = tsedge_get_series_stats(db, names[i], &stats);
        if (rc != TSEDGE_OK) {
            return rc;
        }
        totals->compressed_size_bytes += stats.compressed_size_bytes;
        totals->raw_size_estimate_bytes += stats.raw_size_estimate_bytes;
        totals->segment_count += (uint64_t)stats.segment_count;
        totals->block_count += (uint64_t)stats.block_count;
        totals->indexed_points += (uint64_t)stats.total_indexed_points + (uint64_t)stats.buffered_points;
    }
    return TSEDGE_OK;
}

static const char* mode_name(write_mode mode) {
    switch (mode) {
        case MODE_APPEND:
            return "append_single";
        case MODE_APPEND_HANDLE:
            return "append_handle";
        case MODE_BATCH:
            return "append_batch";
        case MODE_BATCH_HANDLE:
            return "batch_handle";
        default:
            return "unknown";
    }
}

static const char* durability_name(tsedge_durability_mode mode) {
    switch (mode) {
        case TSEDGE_DURABILITY_FAST:
            return "fast";
        case TSEDGE_DURABILITY_BALANCED:
            return "balanced";
        case TSEDGE_DURABILITY_STRICT:
            return "strict";
        default:
            return "unknown";
    }
}

static int run_benchmark(const bench_config* config) {
    if (remove_tree(config->db_path) != 0) {
        fprintf(stderr, "failed to remove old database: %s\n", config->db_path);
        return 1;
    }

    tsedge_db* db = NULL;
    int rc = tsedge_open(config->db_path, &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_open: %s\n", tsedge_strerror(rc));
        return 1;
    }
    rc = tsedge_set_durability(db, config->durability);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_set_durability: %s\n", tsedge_strerror(rc));
        tsedge_close(db);
        return 1;
    }

    char (*names)[64] = (char (*)[64])calloc(config->series_count, sizeof(*names));
    if (!names) {
        tsedge_close(db);
        return 1;
    }

    rc = create_series(db, names, config->series_count);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_create_series: %s\n", tsedge_strerror(rc));
        free(names);
        tsedge_close(db);
        return 1;
    }

    tsedge_series_handle** handles = (tsedge_series_handle**)calloc(config->series_count, sizeof(*handles));
    if (!handles) {
        free(names);
        tsedge_close(db);
        return 1;
    }
    rc = get_handles(db, names, handles, config->series_count);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_get_series_handle: %s\n", tsedge_strerror(rc));
        free(handles);
        free(names);
        tsedge_close(db);
        return 1;
    }

    double append_start = now_seconds();
    switch (config->mode) {
        case MODE_APPEND:
            rc = write_append(db, names, config);
            break;
        case MODE_APPEND_HANDLE:
            rc = write_append_handle(db, handles, config);
            break;
        case MODE_BATCH:
            rc = write_batch(db, names, config);
            break;
        case MODE_BATCH_HANDLE:
            rc = write_batch_handle(db, handles, config);
            break;
        default:
            rc = TSEDGE_ERR_INVALID_ARGUMENT;
            break;
    }
    double append_seconds = now_seconds() - append_start;
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "write: %s\n", tsedge_strerror(rc));
        free(handles);
        free(names);
        tsedge_close(db);
        return 1;
    }

    char wal_path[1024];
    int wal_n = snprintf(wal_path, sizeof(wal_path), "%s/wal.log", config->db_path);
    uint64_t wal_size_bytes = wal_n >= 0 && (size_t)wal_n < sizeof(wal_path)
        ? file_size_bytes(wal_path)
        : 0u;

    double flush_start = now_seconds();
    rc = tsedge_flush_all(db);
    double flush_seconds = now_seconds() - flush_start;
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_flush_all: %s\n", tsedge_strerror(rc));
        free(handles);
        free(names);
        tsedge_close(db);
        return 1;
    }

    storage_totals totals;
    rc = collect_storage_totals(db, names, config->series_count, &totals);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_get_series_stats: %s\n", tsedge_strerror(rc));
        free(handles);
        free(names);
        tsedge_close(db);
        return 1;
    }

    double close_start = now_seconds();
    rc = tsedge_close(db);
    double close_seconds = now_seconds() - close_start;
    db = NULL;
    free(handles);
    free(names);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_close: %s\n", tsedge_strerror(rc));
        return 1;
    }

    uint64_t database_size_bytes = directory_size(config->db_path);
    uint64_t raw_size = totals.raw_size_estimate_bytes;
    if (raw_size == 0) {
        raw_size = (uint64_t)config->points * (uint64_t)sizeof(tsedge_point);
    }
    double total_seconds = append_seconds + flush_seconds + close_seconds;
    double points_per_second = total_seconds > 0.0 ? (double)config->points / total_seconds : 0.0;
    double append_points_per_second = append_seconds > 0.0 ? (double)config->points / append_seconds : 0.0;
    double compression_ratio = totals.compressed_size_bytes > 0
        ? (double)raw_size / (double)totals.compressed_size_bytes
        : 0.0;
    double bytes_per_point = config->points > 0
        ? (double)totals.compressed_size_bytes / (double)config->points
        : 0.0;

    if (config->mode == MODE_APPEND) {
        printf("scenario: append_single\n");
    } else if (config->mode == MODE_APPEND_HANDLE) {
        printf("scenario: append_handle\n");
    } else if (config->mode == MODE_BATCH_HANDLE) {
        printf("scenario: batch_handle_%zu\n", config->batch_size);
    } else {
        printf("scenario: append_batch_%zu\n", config->batch_size);
    }
    printf("mode: %s\n", mode_name(config->mode));
    printf("durability: %s\n", durability_name(config->durability));
    printf("series_count: %zu\n", config->series_count);
    printf("total_points: %zu\n", config->points);
    printf("batch_size: %zu\n", (config->mode == MODE_BATCH || config->mode == MODE_BATCH_HANDLE) ? config->batch_size : 1u);
    printf("append_seconds: %.6f\n", append_seconds);
    printf("flush_seconds: %.6f\n", flush_seconds);
    printf("close_seconds: %.6f\n", close_seconds);
    printf("total_seconds: %.6f\n", total_seconds);
    printf("elapsed_seconds: %.6f\n", total_seconds);
    printf("points_per_second: %.2f\n", points_per_second);
    printf("append_points_per_second: %.2f\n", append_points_per_second);
    printf("wal_size_bytes: %" PRIu64 "\n", wal_size_bytes);
    printf("database_size_bytes: %" PRIu64 "\n", database_size_bytes);
    printf("raw_size_estimate_bytes: %" PRIu64 "\n", raw_size);
    printf("compressed_size_bytes: %" PRIu64 "\n", totals.compressed_size_bytes);
    printf("compression_ratio: %.6f\n", compression_ratio);
    printf("bytes_per_point: %.6f\n", bytes_per_point);
    printf("segment_count: %" PRIu64 "\n", totals.segment_count);
    printf("block_count: %" PRIu64 "\n", totals.block_count);
    printf("indexed_points: %" PRIu64 "\n", totals.indexed_points);
    printf("db_path: %s\n", config->db_path);

    return 0;
}

int main(int argc, char** argv) {
    bench_config config;
    if (!parse_args(argc, argv, &config)) {
        print_help(argv[0]);
        return 1;
    }
    return run_benchmark(&config);
}
