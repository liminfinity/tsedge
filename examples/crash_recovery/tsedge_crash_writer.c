#include "tsedge.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char* default_db_path = "crash_recovery_demo_db";
static const char* series_name = "crash.temperature";

typedef enum {
    WRITE_POINTS,
    WRITE_BATCH
} write_mode;

static void fail_now(const char* operation, int status) {
    fprintf(stderr, "%s failed: %s\n", operation, tsedge_strerror(status));
    fflush(stderr);
    _exit(2);
}

static tsedge_durability_mode parse_durability(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--durability") != 0) {
            continue;
        }
        if (strcmp(argv[i + 1], "fast") == 0) {
            return TSEDGE_DURABILITY_FAST;
        }
        if (strcmp(argv[i + 1], "balanced") == 0) {
            return TSEDGE_DURABILITY_BALANCED;
        }
        if (strcmp(argv[i + 1], "strict") == 0) {
            return TSEDGE_DURABILITY_STRICT;
        }
        fprintf(stderr, "unknown durability: %s\n", argv[i + 1]);
        fflush(stderr);
        _exit(2);
    }
    return TSEDGE_DURABILITY_STRICT;
}

static write_mode parse_mode(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--mode") != 0) {
            continue;
        }
        if (strcmp(argv[i + 1], "point") == 0) {
            return WRITE_POINTS;
        }
        if (strcmp(argv[i + 1], "batch") == 0) {
            return WRITE_BATCH;
        }
        fprintf(stderr, "unknown mode: %s\n", argv[i + 1]);
        fflush(stderr);
        _exit(2);
    }
    return WRITE_POINTS;
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

int main(int argc, char** argv) {
    const char* db_path = (argc > 1 && strncmp(argv[1], "--", 2) != 0) ? argv[1] : default_db_path;
    tsedge_durability_mode durability = parse_durability(argc, argv);
    write_mode mode = parse_mode(argc, argv);
    tsedge_db* db = NULL;

    printf("Crash writer\n");
    printf("database: %s\n", db_path);
    printf("durability: %s\n", durability_name(durability));
    printf("mode: %s\n", mode == WRITE_BATCH ? "batch" : "point");

    int rc = tsedge_open(db_path, &db);
    if (rc != TSEDGE_OK) {
        fail_now("tsedge_open", rc);
    }
    rc = tsedge_set_durability(db, durability);
    if (rc != TSEDGE_OK) {
        fail_now("tsedge_set_durability", rc);
    }
    rc = tsedge_create_series(db, series_name);
    if (rc != TSEDGE_OK) {
        fail_now("tsedge_create_series", rc);
    }

    tsedge_point points[5];
    for (int i = 0; i < 5; ++i) {
        points[i].timestamp = (int64_t)i + 1;
        points[i].value = 11.0 + (double)i;
    }

    if (mode == WRITE_BATCH) {
        rc = tsedge_append_batch(db, series_name, points, 5);
        if (rc != TSEDGE_OK) {
            fail_now("tsedge_append_batch", rc);
        }
    } else {
        for (int i = 0; i < 5; ++i) {
            rc = tsedge_append(db, series_name, points[i].timestamp, points[i].value);
            if (rc != TSEDGE_OK) {
                fail_now("tsedge_append", rc);
            }
        }
    }

    printf("written points: 5\n");
    printf("simulating crash without tsedge_close\n");
    fflush(stdout);

    _exit(1);
}
