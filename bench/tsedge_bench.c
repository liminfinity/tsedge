#include "tsedge.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    /* Smooth data should benefit from timestamp deltas and repeated patterns. */
    DATASET_SMOOTH,

    /* Noisy values are intentionally harder for value compression. */
    DATASET_NOISY,

    /* Step data checks behavior when values repeat for long runs. */
    DATASET_STEP
} dataset_type;

typedef struct {
    size_t count;
} read_counter;

static int count_cb(const tsedge_point* point, void* user_data) {
    (void)point;
    read_counter* counter = (read_counter*)user_data;
    ++counter->count;
    return 0;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void rm_rf(const char* path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    (void)system(cmd);
}

static uint64_t dir_size(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        struct stat st;
        return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
    }
    uint64_t total = 0;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        total += dir_size(child);
    }
    closedir(dir);
    return total;
}

static double sample_value(dataset_type type, size_t i) {
    switch (type) {
        case DATASET_SMOOTH:
            return 70.0 + sin((double)i * 0.001) + ((double)(i % 17) - 8.0) * 0.0001;
        case DATASET_NOISY:
            return (double)rand() / (double)RAND_MAX;
        case DATASET_STEP:
            return 50.0 + (double)((i / 1000) % 20);
        default:
            return 0.0;
    }
}

static const char* dataset_name(dataset_type type) {
    switch (type) {
        case DATASET_SMOOTH:
            return "smooth";
        case DATASET_NOISY:
            return "noisy";
        case DATASET_STEP:
            return "step";
        default:
            return "unknown";
    }
}

static int run_dataset(dataset_type type, size_t points) {
    /*
     * The benchmark reports write/read throughput, aggregate timing and storage
     * size. Results depend on the device, compiler, filesystem and storage.
     */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/tsedge_bench_%s_%ld", dataset_name(type), (long)getpid());
    rm_rf(path);

    tsedge_db* db = NULL;
    int rc = tsedge_open(path, &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "open: %s\n", tsedge_strerror(rc));
        return 1;
    }
    rc = tsedge_create_series(db, "bench.value");
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "create_series: %s\n", tsedge_strerror(rc));
        return 1;
    }

    srand(1);
    double start = now_seconds();
    for (size_t i = 0; i < points; ++i) {
        rc = tsedge_append(db, "bench.value", 1710000000000LL + (int64_t)i * 1000, sample_value(type, i));
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "append: %s\n", tsedge_strerror(rc));
            return 1;
        }
    }
    rc = tsedge_close(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "close: %s\n", tsedge_strerror(rc));
        return 1;
    }
    double write_seconds = now_seconds() - start;

    db = NULL;
    rc = tsedge_open(path, &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "reopen: %s\n", tsedge_strerror(rc));
        return 1;
    }

    read_counter counter;
    counter.count = 0;
    start = now_seconds();
    rc = tsedge_read_range(db, "bench.value", 1710000000000LL, 1710000000000LL + (int64_t)points * 1000, count_cb, &counter);
    double read_seconds = now_seconds() - start;
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "read: %s\n", tsedge_strerror(rc));
        return 1;
    }

    double aggregate = 0.0;
    start = now_seconds();
    rc = tsedge_aggregate(db, "bench.value", 1710000000000LL, 1710000000000LL + (int64_t)points * 1000, TSEDGE_AGG_AVG, &aggregate);
    double aggregate_seconds = now_seconds() - start;
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "aggregate: %s\n", tsedge_strerror(rc));
        return 1;
    }
    rc = tsedge_close(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "close2: %s\n", tsedge_strerror(rc));
        return 1;
    }

    uint64_t db_size = dir_size(path);
    uint64_t raw_size = (uint64_t)points * 16u;
    printf("dataset=%s\n", dataset_name(type));
    printf("points=%zu\n", points);
    printf("write_points_per_sec=%.2f\n", (double)points / write_seconds);
    printf("read_points_per_sec=%.2f\n", (double)counter.count / read_seconds);
    printf("aggregate_seconds=%.6f\n", aggregate_seconds);
    printf("aggregate_avg=%.17g\n", aggregate);
    printf("db_size_bytes=%" PRIu64 "\n", db_size);
    printf("raw_size_bytes=%" PRIu64 "\n", raw_size);
    printf("compression_ratio=%.6f\n\n", db_size == 0 ? 0.0 : (double)raw_size / (double)db_size);
    rm_rf(path);
    return 0;
}

int main(int argc, char** argv) {
    size_t points = 100000;
    if (argc > 1) {
        points = (size_t)strtoull(argv[1], NULL, 10);
    }
    if (run_dataset(DATASET_SMOOTH, points) != 0) {
        return 1;
    }
    if (run_dataset(DATASET_NOISY, points) != 0) {
        return 1;
    }
    if (run_dataset(DATASET_STEP, points) != 0) {
        return 1;
    }
    return 0;
}
