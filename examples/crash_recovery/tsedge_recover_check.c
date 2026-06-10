#include "tsedge.h"

#include <stdio.h>

static const char* default_db_path = "crash_recovery_demo_db";
static const char* series_name = "crash.temperature";

typedef struct {
    tsedge_point points[16];
    size_t count;
} point_vec;

static int collect_point(const tsedge_point* point, void* user_data) {
    point_vec* vec = (point_vec*)user_data;
    if (vec->count < sizeof(vec->points) / sizeof(vec->points[0])) {
        vec->points[vec->count++] = *point;
    }
    return 0;
}

static int check_recovered_points(const point_vec* vec) {
    if (vec->count != 5) {
        fprintf(stderr, "expected 5 recovered points, got %zu\n", vec->count);
        return 1;
    }
    for (size_t i = 0; i < vec->count; ++i) {
        int64_t expected_timestamp = (int64_t)i + 1;
        double expected_value = 11.0 + (double)i;
        if (vec->points[i].timestamp != expected_timestamp || vec->points[i].value != expected_value) {
            fprintf(stderr,
                    "unexpected point %zu: got (%lld, %.1f), expected (%lld, %.1f)\n",
                    i,
                    (long long)vec->points[i].timestamp,
                    vec->points[i].value,
                    (long long)expected_timestamp,
                    expected_value);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    const char* db_path = argc > 1 ? argv[1] : default_db_path;
    tsedge_db* db = NULL;

    int rc = tsedge_open(db_path, &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_open failed: %s\n", tsedge_strerror(rc));
        return 1;
    }

    point_vec vec = {{0}, 0};
    rc = tsedge_read_range(db, series_name, 1, 5, collect_point, &vec);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_read_range failed: %s\n", tsedge_strerror(rc));
        tsedge_close(db);
        return 1;
    }

    int failed = check_recovered_points(&vec);
    if (failed) {
        tsedge_close(db);
        return 1;
    }

    printf("Recovery check\n");
    printf("database: %s\n", db_path);
    printf("recovered points: %zu\n", vec.count);
    printf("WAL recovery: ok\n");

    rc = tsedge_close(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_close failed: %s\n", tsedge_strerror(rc));
        return 1;
    }
    return 0;
}
