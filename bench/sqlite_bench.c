#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    DATASET_SMOOTH,
    DATASET_NOISY,
    DATASET_STEP
} dataset_type;

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

static uint64_t file_size(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
}

static int exec_sql(sqlite3* db, const char* sql) {
    char* err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "sqlite error: %s\n", err ? err : sqlite3_errmsg(db));
        sqlite3_free(err);
        return 1;
    }
    return 0;
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

static int load_data(sqlite3* db, dataset_type type, size_t points) {
    if (exec_sql(db, "BEGIN TRANSACTION;") != 0) {
        return 1;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, "INSERT INTO points(timestamp, value) VALUES (?, ?);", -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite prepare insert: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    srand(1);
    for (size_t i = 0; i < points; ++i) {
        sqlite3_bind_int64(stmt, 1, 1710000000000LL + (sqlite3_int64)i * 1000);
        sqlite3_bind_double(stmt, 2, sample_value(type, i));
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "sqlite insert: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return 1;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    sqlite3_finalize(stmt);
    return exec_sql(db, "COMMIT;");
}

static int read_count(sqlite3* db, size_t* out_count) {
    sqlite3_stmt* stmt = NULL;
    const char* sql = "SELECT timestamp, value FROM points WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite prepare read: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_int64(stmt, 1, 1710000000000LL);
    sqlite3_bind_int64(stmt, 2, 1710000000000LL + 9223372036854LL);

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ++count;
    }
    sqlite3_finalize(stmt);
    *out_count = count;
    return 0;
}

static int read_avg(sqlite3* db, double* out_avg) {
    sqlite3_stmt* stmt = NULL;
    const char* sql = "SELECT AVG(value) FROM points WHERE timestamp BETWEEN ? AND ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite prepare avg: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_int64(stmt, 1, 1710000000000LL);
    sqlite3_bind_int64(stmt, 2, 1710000000000LL + 9223372036854LL);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out_avg = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int run_dataset(dataset_type type, size_t points, FILE* csv) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/sqlite_bench_%s_%ld.sqlite3", dataset_name(type), (long)getpid());
    remove(path);

    sqlite3* db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        fprintf(stderr, "sqlite open: %s\n", db ? sqlite3_errmsg(db) : "no handle");
        sqlite3_close(db);
        return 1;
    }

    if (exec_sql(db, "CREATE TABLE points(timestamp INTEGER NOT NULL, value REAL NOT NULL);") != 0 ||
        exec_sql(db, "CREATE INDEX idx_points_timestamp ON points(timestamp);") != 0) {
        sqlite3_close(db);
        remove(path);
        return 1;
    }

    double start = now_seconds();
    if (load_data(db, type, points) != 0) {
        sqlite3_close(db);
        remove(path);
        return 1;
    }
    double write_seconds = now_seconds() - start;

    size_t count = 0;
    start = now_seconds();
    if (read_count(db, &count) != 0) {
        sqlite3_close(db);
        remove(path);
        return 1;
    }
    double read_seconds = now_seconds() - start;

    double avg = 0.0;
    start = now_seconds();
    if (read_avg(db, &avg) != 0) {
        sqlite3_close(db);
        remove(path);
        return 1;
    }
    double avg_seconds = now_seconds() - start;
    (void)avg;

    sqlite3_close(db);

    bench_result result;
    result.system = "sqlite";
    result.dataset = dataset_name(type);
    result.points = count;
    result.write_seconds = write_seconds;
    result.read_seconds = read_seconds;
    result.avg_seconds = avg_seconds;
    result.size_bytes = file_size(path);
    result.raw_size_bytes = (uint64_t)points * 16u;
    print_result(&result);
    int rc = write_csv_result(csv, &result);
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
