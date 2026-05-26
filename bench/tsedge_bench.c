#define _POSIX_C_SOURCE 200809L

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
    DATASET_STEP,

    /* Constant data is the best case for XOR value compression. */
    DATASET_CONSTANT,

    /* Irregular timestamps stress delta-of-delta timestamp compression. */
    DATASET_IRREGULAR_TIMESTAMPS
} dataset_type;

typedef struct {
    size_t count;
} read_counter;

typedef struct {
    const char* system;
    const char* dataset;
    size_t points;
    double write_seconds;
    double read_seconds;
    double avg_seconds;
    uint64_t size_bytes;
    uint64_t raw_size_bytes;
} bench_result;

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
    int rc = system(cmd);
    (void)rc;
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
        case DATASET_CONSTANT:
            return 42.0;
        case DATASET_IRREGULAR_TIMESTAMPS:
            return 65.0 + sin((double)i * 0.002);
        default:
            return 0.0;
    }
}

static int64_t sample_timestamp(dataset_type type, size_t i) {
    int64_t base = 1710000000000LL;
    if (type == DATASET_IRREGULAR_TIMESTAMPS) {
        int64_t jitter = (int64_t)((i % 11u) * (i % 11u) * 37u);
        return base + (int64_t)i * 1000 + jitter;
    }
    return base + (int64_t)i * 1000;
}

static int64_t range_end_timestamp(dataset_type type, size_t points) {
    if (points == 0) {
        return sample_timestamp(type, 0);
    }
    return sample_timestamp(type, points - 1u) + 5000;
}

static const char* dataset_name(dataset_type type) {
    switch (type) {
        case DATASET_SMOOTH:
            return "smooth";
        case DATASET_NOISY:
            return "noisy";
        case DATASET_STEP:
            return "step";
        case DATASET_CONSTANT:
            return "constant";
        case DATASET_IRREGULAR_TIMESTAMPS:
            return "irregular_timestamps";
        default:
            return "unknown";
    }
}

static int write_csv_header(FILE* csv) {
    return fprintf(
        csv,
        "system,dataset,points,write_seconds,write_points_per_sec,read_seconds,read_points_per_sec,avg_seconds,size_bytes,raw_size_bytes,compression_ratio\n"
    ) < 0 ? 1 : 0;
}

static void print_result(const bench_result* result) {
    double write_pps = result->write_seconds > 0.0 ? (double)result->points / result->write_seconds : 0.0;
    double read_pps = result->read_seconds > 0.0 ? (double)result->points / result->read_seconds : 0.0;
    double ratio = result->size_bytes == 0 ? 0.0 : (double)result->raw_size_bytes / (double)result->size_bytes;

    printf("system=%s\n", result->system);
    printf("dataset=%s\n", result->dataset);
    printf("points=%zu\n", result->points);
    printf("write_seconds=%.6f\n", result->write_seconds);
    printf("write_points_per_sec=%.2f\n", write_pps);
    printf("read_seconds=%.6f\n", result->read_seconds);
    printf("read_points_per_sec=%.2f\n", read_pps);
    printf("avg_seconds=%.6f\n", result->avg_seconds);
    printf("size_bytes=%" PRIu64 "\n", result->size_bytes);
    printf("raw_size_bytes=%" PRIu64 "\n", result->raw_size_bytes);
    printf("compression_ratio=%.6f\n\n", ratio);
}

static int write_csv_result(FILE* csv, const bench_result* result) {
    if (!csv) {
        return 0;
    }
    double write_pps = result->write_seconds > 0.0 ? (double)result->points / result->write_seconds : 0.0;
    double read_pps = result->read_seconds > 0.0 ? (double)result->points / result->read_seconds : 0.0;
    double ratio = result->size_bytes == 0 ? 0.0 : (double)result->raw_size_bytes / (double)result->size_bytes;
    return fprintf(
        csv,
        "%s,%s,%zu,%.9f,%.2f,%.9f,%.2f,%.9f,%" PRIu64 ",%" PRIu64 ",%.9f\n",
        result->system,
        result->dataset,
        result->points,
        result->write_seconds,
        write_pps,
        result->read_seconds,
        read_pps,
        result->avg_seconds,
        result->size_bytes,
        result->raw_size_bytes,
        ratio
    ) < 0 ? 1 : 0;
}

static int run_dataset(dataset_type type, size_t points, FILE* csv) {
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
        rc = tsedge_append(db, "bench.value", sample_timestamp(type, i), sample_value(type, i));
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
    rc = tsedge_read_range(db, "bench.value", sample_timestamp(type, 0), range_end_timestamp(type, points), count_cb, &counter);
    double read_seconds = now_seconds() - start;
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "read: %s\n", tsedge_strerror(rc));
        return 1;
    }

    double aggregate = 0.0;
    start = now_seconds();
    rc = tsedge_aggregate(db, "bench.value", sample_timestamp(type, 0), range_end_timestamp(type, points), TSEDGE_AGG_AVG, &aggregate);
    double avg_seconds = now_seconds() - start;
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "aggregate: %s\n", tsedge_strerror(rc));
        return 1;
    }
    rc = tsedge_close(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "close2: %s\n", tsedge_strerror(rc));
        return 1;
    }

    (void)aggregate;
    bench_result result;
    result.system = "tsedge";
    result.dataset = dataset_name(type);
    result.points = counter.count;
    result.write_seconds = write_seconds;
    result.read_seconds = read_seconds;
    result.avg_seconds = avg_seconds;
    result.size_bytes = dir_size(path);
    result.raw_size_bytes = (uint64_t)points * 16u;
    print_result(&result);
    if (write_csv_result(csv, &result) != 0) {
        rm_rf(path);
        return 1;
    }
    rm_rf(path);
    return 0;
}

int main(int argc, char** argv) {
    size_t points = 100000;
    if (argc > 1) {
        points = (size_t)strtoull(argv[1], NULL, 10);
    }
    FILE* csv = NULL;
    if (argc > 2) {
        csv = fopen(argv[2], "w");
        if (!csv) {
            perror("fopen csv");
            return 1;
        }
        if (write_csv_header(csv) != 0) {
            fclose(csv);
            return 1;
        }
    }
    dataset_type datasets[] = {
        DATASET_SMOOTH,
        DATASET_NOISY,
        DATASET_STEP,
        DATASET_CONSTANT,
        DATASET_IRREGULAR_TIMESTAMPS
    };
    for (size_t i = 0; i < sizeof(datasets) / sizeof(datasets[0]); ++i) {
        if (run_dataset(datasets[i], points, csv) != 0) {
            if (csv) {
                fclose(csv);
            }
            return 1;
        }
    }
    if (csv && fclose(csv) != 0) {
        return 1;
    }
    return 0;
}
