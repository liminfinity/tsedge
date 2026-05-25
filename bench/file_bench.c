#define _POSIX_C_SOURCE 200809L

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
    DATASET_SMOOTH,
    DATASET_NOISY,
    DATASET_STEP
} dataset_type;

typedef enum {
    FORMAT_RAW,
    FORMAT_CSV
} file_format;

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

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
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

static const char* format_name(file_format format) {
    return format == FORMAT_RAW ? "raw_binary" : "csv";
}

static uint64_t file_size(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
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

static int write_raw(const char* path, dataset_type type, size_t points) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return 1;
    }
    for (size_t i = 0; i < points; ++i) {
        int64_t timestamp = 1710000000000LL + (int64_t)i * 1000;
        double value = sample_value(type, i);
        if (fwrite(&timestamp, sizeof(timestamp), 1, f) != 1 || fwrite(&value, sizeof(value), 1, f) != 1) {
            fclose(f);
            return 1;
        }
    }
    return fclose(f) == 0 ? 0 : 1;
}

static int write_text_csv(const char* path, dataset_type type, size_t points) {
    FILE* f = fopen(path, "w");
    if (!f) {
        return 1;
    }
    if (fprintf(f, "timestamp,value\n") < 0) {
        fclose(f);
        return 1;
    }
    for (size_t i = 0; i < points; ++i) {
        int64_t timestamp = 1710000000000LL + (int64_t)i * 1000;
        if (fprintf(f, "%" PRId64 ",%.17g\n", timestamp, sample_value(type, i)) < 0) {
            fclose(f);
            return 1;
        }
    }
    return fclose(f) == 0 ? 0 : 1;
}

static int read_raw_count(const char* path, size_t* out_count) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return 1;
    }
    size_t count = 0;
    for (;;) {
        int64_t timestamp = 0;
        double value = 0.0;
        if (fread(&timestamp, sizeof(timestamp), 1, f) != 1) {
            break;
        }
        if (fread(&value, sizeof(value), 1, f) != 1) {
            fclose(f);
            return 1;
        }
        (void)timestamp;
        (void)value;
        ++count;
    }
    *out_count = count;
    return fclose(f) == 0 ? 0 : 1;
}

static int read_csv_count(const char* path, size_t* out_count) {
    FILE* f = fopen(path, "r");
    if (!f) {
        return 1;
    }
    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return 1;
    }
    size_t count = 0;
    while (fgets(line, sizeof(line), f)) {
        ++count;
    }
    *out_count = count;
    return fclose(f) == 0 ? 0 : 1;
}

static int avg_raw(const char* path, double* out_avg) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return 1;
    }
    double sum = 0.0;
    size_t count = 0;
    for (;;) {
        int64_t timestamp = 0;
        double value = 0.0;
        if (fread(&timestamp, sizeof(timestamp), 1, f) != 1) {
            break;
        }
        if (fread(&value, sizeof(value), 1, f) != 1) {
            fclose(f);
            return 1;
        }
        (void)timestamp;
        sum += value;
        ++count;
    }
    *out_avg = count == 0 ? 0.0 : sum / (double)count;
    return fclose(f) == 0 ? 0 : 1;
}

static int avg_csv(const char* path, double* out_avg) {
    FILE* f = fopen(path, "r");
    if (!f) {
        return 1;
    }
    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return 1;
    }
    double sum = 0.0;
    size_t count = 0;
    while (fgets(line, sizeof(line), f)) {
        long long timestamp = 0;
        double value = 0.0;
        if (sscanf(line, "%lld,%lf", &timestamp, &value) == 2) {
            (void)timestamp;
            sum += value;
            ++count;
        }
    }
    *out_avg = count == 0 ? 0.0 : sum / (double)count;
    return fclose(f) == 0 ? 0 : 1;
}

static int run_one(file_format format, dataset_type type, size_t points, FILE* csv) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/file_bench_%s_%s_%ld.%s", format_name(format), dataset_name(type), (long)getpid(), format == FORMAT_RAW ? "raw" : "csv");
    remove(path);

    srand(1);
    double start = now_seconds();
    int rc = format == FORMAT_RAW ? write_raw(path, type, points) : write_text_csv(path, type, points);
    double write_seconds = now_seconds() - start;
    if (rc != 0) {
        fprintf(stderr, "write failed: %s\n", path);
        remove(path);
        return 1;
    }

    size_t read_count = 0;
    start = now_seconds();
    rc = format == FORMAT_RAW ? read_raw_count(path, &read_count) : read_csv_count(path, &read_count);
    double read_seconds = now_seconds() - start;
    if (rc != 0) {
        fprintf(stderr, "read failed: %s\n", path);
        remove(path);
        return 1;
    }

    double avg = 0.0;
    start = now_seconds();
    rc = format == FORMAT_RAW ? avg_raw(path, &avg) : avg_csv(path, &avg);
    double avg_seconds = now_seconds() - start;
    if (rc != 0) {
        fprintf(stderr, "avg failed: %s\n", path);
        remove(path);
        return 1;
    }
    (void)avg;

    bench_result result;
    result.system = format_name(format);
    result.dataset = dataset_name(type);
    result.points = read_count;
    result.write_seconds = write_seconds;
    result.read_seconds = read_seconds;
    result.avg_seconds = avg_seconds;
    result.size_bytes = file_size(path);
    result.raw_size_bytes = (uint64_t)points * 16u;
    print_result(&result);
    rc = write_csv_result(csv, &result);
    remove(path);
    return rc;
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

    dataset_type datasets[] = {DATASET_SMOOTH, DATASET_NOISY, DATASET_STEP};
    file_format formats[] = {FORMAT_RAW, FORMAT_CSV};
    for (size_t f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
        for (size_t d = 0; d < sizeof(datasets) / sizeof(datasets[0]); ++d) {
            if (run_one(formats[f], datasets[d], points, csv) != 0) {
                if (csv) {
                    fclose(csv);
                }
                return 1;
            }
        }
    }

    if (csv && fclose(csv) != 0) {
        return 1;
    }
    return 0;
}
