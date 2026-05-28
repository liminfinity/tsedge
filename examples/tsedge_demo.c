#define _POSIX_C_SOURCE 200809L

#include "tsedge.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEMO_DB_PATH "./demo_db"
#define DEMO_CSV_PATH "temperature.csv"
#define DEMO_POINTS 50000u
#define DEMO_BATCH_SIZE 1024u
#define DEMO_BASE_TS 1710000000000LL

typedef struct {
    size_t count;
    size_t printed;
} read_preview;

static int remove_tree(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path);
        if (!dir) {
            return -1;
        }
        struct dirent* entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char child[1024];
            int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            if (n <= 0 || (size_t)n >= sizeof(child) || remove_tree(child) != 0) {
                closedir(dir);
                return -1;
            }
        }
        if (closedir(dir) != 0) {
            return -1;
        }
        return rmdir(path);
    }

    return unlink(path);
}

static int preview_cb(const tsedge_point* point, void* user_data) {
    read_preview* preview = (read_preview*)user_data;
    if (preview->printed < 5u) {
        printf("  point[%zu]: timestamp=%lld value=%.6f\n",
               preview->printed,
               (long long)point->timestamp,
               point->value);
        ++preview->printed;
    }
    ++preview->count;
    return 0;
}

static double temperature_value(size_t i) {
    return 70.0 + (double)(i % 200u) * 0.01 + (double)(i / 1000u) * 0.02;
}

static double current_value(size_t i) {
    return 9.5 + (double)(i % 50u) * 0.03;
}

static double vibration_value(size_t i) {
    return (double)((i / 250u) % 8u) * 0.125;
}

static int create_series(tsedge_db* db, const char* name) {
    int rc = tsedge_create_series(db, name);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "create_series(%s) failed: %s\n", name, tsedge_strerror(rc));
    }
    return rc;
}

static int create_demo_series(tsedge_db* db) {
    if (create_series(db, "motor.temperature") != TSEDGE_OK ||
        create_series(db, "motor.current") != TSEDGE_OK ||
        create_series(db, "motor.vibration") != TSEDGE_OK) {
        return 1;
    }
    printf("created series: motor.temperature, motor.current, motor.vibration\n");
    return 0;
}

static int append_initial_points(tsedge_db* db) {
    for (size_t i = 0; i < 100u; ++i) {
        int64_t ts = DEMO_BASE_TS + (int64_t)i * 1000;
        int rc = tsedge_append(db, "motor.temperature", ts, temperature_value(i));
        if (rc == TSEDGE_OK) {
            rc = tsedge_append(db, "motor.current", ts, current_value(i));
        }
        if (rc == TSEDGE_OK) {
            rc = tsedge_append(db, "motor.vibration", ts, vibration_value(i));
        }
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "append failed: %s\n", tsedge_strerror(rc));
            return rc;
        }
    }
    return TSEDGE_OK;
}

static int append_batch_points(tsedge_db* db) {
    tsedge_point* temperature = (tsedge_point*)malloc(DEMO_BATCH_SIZE * sizeof(*temperature));
    tsedge_point* current = (tsedge_point*)malloc(DEMO_BATCH_SIZE * sizeof(*current));
    tsedge_point* vibration = (tsedge_point*)malloc(DEMO_BATCH_SIZE * sizeof(*vibration));
    if (!temperature || !current || !vibration) {
        free(temperature);
        free(current);
        free(vibration);
        return TSEDGE_ERR_NO_MEMORY;
    }

    for (size_t offset = 100u; offset < DEMO_POINTS;) {
        size_t chunk = DEMO_POINTS - offset;
        if (chunk > DEMO_BATCH_SIZE) {
            chunk = DEMO_BATCH_SIZE;
        }
        for (size_t j = 0; j < chunk; ++j) {
            size_t i = offset + j;
            int64_t ts = DEMO_BASE_TS + (int64_t)i * 1000;
            temperature[j].timestamp = ts;
            temperature[j].value = temperature_value(i);
            current[j].timestamp = ts;
            current[j].value = current_value(i);
            vibration[j].timestamp = ts;
            vibration[j].value = vibration_value(i);
        }

        int rc = tsedge_append_batch(db, "motor.temperature", temperature, chunk);
        if (rc == TSEDGE_OK) {
            rc = tsedge_append_batch(db, "motor.current", current, chunk);
        }
        if (rc == TSEDGE_OK) {
            rc = tsedge_append_batch(db, "motor.vibration", vibration, chunk);
        }
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "append_batch failed: %s\n", tsedge_strerror(rc));
            free(temperature);
            free(current);
            free(vibration);
            return rc;
        }
        offset += chunk;
    }

    free(temperature);
    free(current);
    free(vibration);
    return TSEDGE_OK;
}

static int write_demo_data(tsedge_db* db) {
    int rc = append_initial_points(db);
    if (rc == TSEDGE_OK) {
        rc = append_batch_points(db);
    }
    if (rc != TSEDGE_OK) {
        return rc;
    }
    printf("appended points per series: %u\n", DEMO_POINTS);
    printf("write modes: first 100 points via tsedge_append, remaining points via tsedge_append_batch\n");
    return TSEDGE_OK;
}

static int load_stats(tsedge_db* db, const char* name, tsedge_series_stats* stats) {
    int rc = tsedge_get_series_stats(db, name, stats);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "get_series_stats(%s) failed: %s\n", name, tsedge_strerror(rc));
    }
    return rc;
}

static void print_loaded_stats(const char* name, const tsedge_series_stats* stats) {
    printf("series: %s\n", name);
    printf("  segments: %zu\n", stats->segment_count);
    printf("  active segment: %u\n", stats->active_segment_id);
    printf("  blocks: %zu\n", stats->block_count);
    printf("  indexed points: %zu\n", stats->total_indexed_points);
    printf("  buffered points: %zu\n", stats->buffered_points);
    if (stats->has_time_range) {
        printf("  time range: %lld .. %lld\n",
               (long long)stats->min_timestamp,
               (long long)stats->max_timestamp);
    }
    printf("  total segment bytes: %llu\n", (unsigned long long)stats->total_segment_size_bytes);
}

static int print_stats(tsedge_db* db, const char* name) {
    tsedge_series_stats stats;
    int rc = load_stats(db, name, &stats);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    print_loaded_stats(name, &stats);
    return TSEDGE_OK;
}

static int print_all_stats(tsedge_db* db) {
    if (print_stats(db, "motor.temperature") != TSEDGE_OK ||
        print_stats(db, "motor.current") != TSEDGE_OK ||
        print_stats(db, "motor.vibration") != TSEDGE_OK) {
        return 1;
    }
    return 0;
}

static int print_aggregates(tsedge_db* db) {
    int64_t from_ts = DEMO_BASE_TS;
    int64_t to_ts = DEMO_BASE_TS + (int64_t)(DEMO_POINTS - 1u) * 1000;
    double min_value = 0.0;
    double max_value = 0.0;
    double avg_value = 0.0;
    double count_value = 0.0;

    int rc = tsedge_aggregate(db, "motor.temperature", from_ts, to_ts, TSEDGE_AGG_MIN, &min_value);
    if (rc == TSEDGE_OK) {
        rc = tsedge_aggregate(db, "motor.temperature", from_ts, to_ts, TSEDGE_AGG_MAX, &max_value);
    }
    if (rc == TSEDGE_OK) {
        rc = tsedge_aggregate(db, "motor.temperature", from_ts, to_ts, TSEDGE_AGG_AVG, &avg_value);
    }
    if (rc == TSEDGE_OK) {
        rc = tsedge_aggregate(db, "motor.temperature", from_ts, to_ts, TSEDGE_AGG_COUNT, &count_value);
    }
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "aggregate failed: %s\n", tsedge_strerror(rc));
        return rc;
    }

    printf("temperature aggregates:\n");
    printf("  min temperature: %.6f\n", min_value);
    printf("  max temperature: %.6f\n", max_value);
    printf("  avg temperature: %.6f\n", avg_value);
    printf("  count: %.0f\n", count_value);
    return TSEDGE_OK;
}

static int preview_temperature_range(tsedge_db* db) {
    read_preview preview;
    memset(&preview, 0, sizeof(preview));
    int rc = tsedge_read_range(
        db,
        "motor.temperature",
        DEMO_BASE_TS + 1000LL * 120,
        DEMO_BASE_TS + 1000LL * 139,
        preview_cb,
        &preview
    );
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "read_range failed: %s\n", tsedge_strerror(rc));
        return rc;
    }
    printf("range read count: %zu\n", preview.count);
    return TSEDGE_OK;
}

static int run_retention_check(tsedge_db* db, int64_t* out_delete_before_ts) {
    tsedge_series_stats before_retention;
    tsedge_series_stats after_retention;
    int rc = load_stats(db, "motor.temperature", &before_retention);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    int64_t delete_before_ts = DEMO_BASE_TS + 1000LL * 25000;
    printf("before retention:\n");
    print_loaded_stats("motor.temperature", &before_retention);
    printf("delete_before: %lld\n", (long long)delete_before_ts);

    rc = tsedge_delete_before(db, "motor.temperature", delete_before_ts);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "delete_before failed: %s\n", tsedge_strerror(rc));
        return rc;
    }
    rc = load_stats(db, "motor.temperature", &after_retention);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    printf("after retention:\n");
    print_loaded_stats("motor.temperature", &after_retention);
    printf("retention check: %s\n",
           after_retention.segment_count <= before_retention.segment_count ? "ok" : "unexpected growth");
    *out_delete_before_ts = delete_before_ts;
    return TSEDGE_OK;
}

static int export_temperature_csv(tsedge_db* db, int64_t from_timestamp) {
    int rc = tsedge_export_csv(
        db,
        "motor.temperature",
        from_timestamp,
        DEMO_BASE_TS + (int64_t)(DEMO_POINTS - 1u) * 1000,
        DEMO_CSV_PATH
    );
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "export_csv failed: %s\n", tsedge_strerror(rc));
        return rc;
    }
    printf("exported: %s\n", DEMO_CSV_PATH);
    return TSEDGE_OK;
}

static int reopen_check(void) {
    tsedge_db* db = NULL;
    int rc = tsedge_open(DEMO_DB_PATH, &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "reopen failed: %s\n", tsedge_strerror(rc));
        return rc;
    }
    printf("reopened database\n");

    if (print_stats(db, "motor.temperature") != TSEDGE_OK ||
        print_aggregates(db) != TSEDGE_OK) {
        tsedge_close(db);
        return TSEDGE_ERR_INTERNAL;
    }

    rc = tsedge_close(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "close after reopen failed: %s\n", tsedge_strerror(rc));
        return rc;
    }

    printf("reopen check: ok\n");
    return TSEDGE_OK;
}

int main(void) {
    printf("TSEdge demo\n");
    printf("database: %s\n", DEMO_DB_PATH);

    if (remove_tree(DEMO_DB_PATH) != 0) {
        fprintf(stderr, "failed to remove old %s\n", DEMO_DB_PATH);
        return 1;
    }
    (void)unlink(DEMO_CSV_PATH);

    tsedge_db* db = NULL;
    int rc = tsedge_open(DEMO_DB_PATH, &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "open failed: %s\n", tsedge_strerror(rc));
        return 1;
    }
    printf("opened database\n");

    if (create_demo_series(db) != 0) {
        tsedge_close(db);
        return 1;
    }

    rc = write_demo_data(db);
    if (rc != TSEDGE_OK) {
        tsedge_close(db);
        return 1;
    }

    rc = preview_temperature_range(db);
    if (rc != TSEDGE_OK) {
        tsedge_close(db);
        return 1;
    }

    if (print_aggregates(db) != TSEDGE_OK || print_all_stats(db) != 0) {
        tsedge_close(db);
        return 1;
    }

    int64_t delete_before_ts = 0;
    rc = run_retention_check(db, &delete_before_ts);
    if (rc != TSEDGE_OK) {
        tsedge_close(db);
        return 1;
    }

    if (print_aggregates(db) != TSEDGE_OK) {
        tsedge_close(db);
        return 1;
    }

    rc = export_temperature_csv(db, delete_before_ts);
    if (rc != TSEDGE_OK) {
        tsedge_close(db);
        return 1;
    }

    rc = tsedge_close(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "close failed: %s\n", tsedge_strerror(rc));
        return 1;
    }
    printf("closed database\n");

    rc = reopen_check();
    if (rc != TSEDGE_OK) {
        return 1;
    }

    printf("done\n");
    return 0;
}
