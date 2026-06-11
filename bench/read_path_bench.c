#define _POSIX_C_SOURCE 200809L

#include "db.h"
#include "series_query.h"
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
    SCENARIO_READ_RANGE_TINY,
    SCENARIO_READ_RANGE_SMALL,
    SCENARIO_READ_RANGE_MEDIUM,
    SCENARIO_READ_RANGE_LARGE,
    SCENARIO_READ_RANGE_FULL,
    SCENARIO_AGGREGATE_AVG_TINY,
    SCENARIO_AGGREGATE_AVG_MEDIUM,
    SCENARIO_AGGREGATE_AVG_FULL,
    SCENARIO_AGGREGATE_MIN_MAX_TINY,
    SCENARIO_AGGREGATE_MIN_MAX_MEDIUM,
    SCENARIO_AGGREGATE_MIN_MAX_FULL,
    SCENARIO_WINDOW_AGGREGATE
} read_scenario;

typedef struct {
    size_t points;
    size_t series_count;
    read_scenario scenario;
    int scenario_all;
    size_t range_size;
    size_t window_size;
    size_t target_windows;
    size_t repeat;
    const char* db_path;
    int keep_db;
} bench_config;

typedef struct {
    size_t points_read;
    double value_sum;
} read_accumulator;

typedef struct {
    double query_seconds;
    size_t points_read;
    double value_sum;
    double result_value;
    double result_min;
    double result_max;
    size_t result_count;
    size_t window_count;
    size_t window_size;
    double window_avg_sum;
    uint64_t window_count_sum;
    int has_first_window;
    tsedge_window_aggregate first_window;
    tsedge_window_aggregate last_window;
    tsedge_read_debug_stats stats;
} scenario_result;

static const read_scenario all_scenarios[] = {
    SCENARIO_READ_RANGE_TINY,
    SCENARIO_READ_RANGE_SMALL,
    SCENARIO_READ_RANGE_MEDIUM,
    SCENARIO_READ_RANGE_LARGE,
    SCENARIO_READ_RANGE_FULL,
    SCENARIO_AGGREGATE_AVG_TINY,
    SCENARIO_AGGREGATE_AVG_MEDIUM,
    SCENARIO_AGGREGATE_AVG_FULL,
    SCENARIO_AGGREGATE_MIN_MAX_TINY,
    SCENARIO_AGGREGATE_MIN_MAX_MEDIUM,
    SCENARIO_AGGREGATE_MIN_MAX_FULL,
    SCENARIO_WINDOW_AGGREGATE,
};

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void print_help(const char* program) {
    printf("Usage: %s [options]\n", program);
    printf("Options:\n");
    printf("  --points N       Total points to generate (default: 1000000)\n");
    printf("  --series N       Number of series (default: 1)\n");
    printf("  --scenario NAME  Scenario name or all (default: all)\n");
    printf("  --range-size N   Override range size for the selected scenario\n");
    printf("  --window-size N  Window size for window_aggregate\n");
    printf("  --target-windows N  Derive window size for window_aggregate (default: 1000)\n");
    printf("  --repeat N       Repeat each query scenario (default: 5)\n");
    printf("  --db-path PATH   Benchmark database path (default: /tmp/tsedge_read_bench_db)\n");
    printf("  --keep-db        Keep generated database after the run\n");
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

static const char* scenario_name(read_scenario scenario) {
    switch (scenario) {
        case SCENARIO_READ_RANGE_TINY:
            return "read_range_tiny";
        case SCENARIO_READ_RANGE_SMALL:
            return "read_range_small";
        case SCENARIO_READ_RANGE_MEDIUM:
            return "read_range_medium";
        case SCENARIO_READ_RANGE_LARGE:
            return "read_range_large";
        case SCENARIO_READ_RANGE_FULL:
            return "read_range_full";
        case SCENARIO_AGGREGATE_AVG_TINY:
            return "aggregate_avg_tiny";
        case SCENARIO_AGGREGATE_AVG_MEDIUM:
            return "aggregate_avg_medium";
        case SCENARIO_AGGREGATE_AVG_FULL:
            return "aggregate_avg_full";
        case SCENARIO_AGGREGATE_MIN_MAX_TINY:
            return "aggregate_min_max_tiny";
        case SCENARIO_AGGREGATE_MIN_MAX_MEDIUM:
            return "aggregate_min_max_medium";
        case SCENARIO_AGGREGATE_MIN_MAX_FULL:
            return "aggregate_min_max_full";
        case SCENARIO_WINDOW_AGGREGATE:
            return "window_aggregate";
        default:
            return "unknown";
    }
}

static int parse_scenario(const char* text, read_scenario* out, int* out_all) {
    if (strcmp(text, "all") == 0) {
        *out_all = 1;
        return 1;
    }
    for (size_t i = 0; i < sizeof(all_scenarios) / sizeof(all_scenarios[0]); ++i) {
        if (strcmp(text, scenario_name(all_scenarios[i])) == 0) {
            *out = all_scenarios[i];
            *out_all = 0;
            return 1;
        }
    }
    return 0;
}

static int scenario_is_read(read_scenario scenario) {
    return scenario >= SCENARIO_READ_RANGE_TINY && scenario <= SCENARIO_READ_RANGE_FULL;
}

static int scenario_is_avg(read_scenario scenario) {
    return scenario >= SCENARIO_AGGREGATE_AVG_TINY && scenario <= SCENARIO_AGGREGATE_AVG_FULL;
}

static int scenario_is_window(read_scenario scenario) {
    return scenario == SCENARIO_WINDOW_AGGREGATE;
}

static const char* aggregate_name(read_scenario scenario) {
    if (scenario_is_read(scenario)) {
        return "none";
    }
    if (scenario_is_window(scenario)) {
        return "window";
    }
    return scenario_is_avg(scenario) ? "avg" : "min_max";
}

static int parse_args(int argc, char** argv, bench_config* config) {
    config->points = 1000000u;
    config->series_count = 1u;
    config->scenario = SCENARIO_READ_RANGE_FULL;
    config->scenario_all = 1;
    config->range_size = 0;
    config->window_size = 0;
    config->target_windows = 1000u;
    config->repeat = 5u;
    config->db_path = "/tmp/tsedge_read_bench_db";
    config->keep_db = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--points") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->points)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--series") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->series_count)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            if (!parse_scenario(argv[++i], &config->scenario, &config->scenario_all)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--range-size") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->range_size)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--window-size") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->window_size)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--target-windows") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->target_windows)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &config->repeat)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--db-path") == 0 && i + 1 < argc) {
            config->db_path = argv[++i];
        } else if (strcmp(argv[i], "--keep-db") == 0) {
            config->keep_db = 1;
        } else {
            return 0;
        }
    }

    return config->points > 0 && config->series_count > 0 && config->repeat > 0;
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

static int make_series_name(size_t index, char* out, size_t out_size) {
    int n = snprintf(out, out_size, "bench.series_%02zu", index);
    return n >= 0 && (size_t)n < out_size;
}

static size_t points_for_series(const bench_config* config, size_t series_index) {
    size_t base = config->points / config->series_count;
    size_t remainder = config->points % config->series_count;
    return base + (series_index < remainder ? 1u : 0u);
}

static double sample_value(size_t series_index, size_t local_index) {
    return (double)(local_index % 1000u) / 10.0 + (double)series_index * 0.001;
}

static int generate_database(const bench_config* config, char (*names)[64]) {
    tsedge_db* db = NULL;
    int rc = tsedge_open(config->db_path, &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_open: %s\n", tsedge_strerror(rc));
        return rc;
    }
    rc = tsedge_set_durability(db, TSEDGE_DURABILITY_FAST);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_set_durability: %s\n", tsedge_strerror(rc));
        tsedge_close(db);
        return rc;
    }

    const size_t batch_size = 4096u;
    tsedge_point* batch = (tsedge_point*)malloc(batch_size * sizeof(*batch));
    if (!batch) {
        tsedge_close(db);
        return TSEDGE_ERR_NO_MEMORY;
    }

    for (size_t series_index = 0; series_index < config->series_count; ++series_index) {
        if (!make_series_name(series_index, names[series_index], 64u)) {
            free(batch);
            tsedge_close(db);
            return TSEDGE_ERR_INVALID_ARGUMENT;
        }
        rc = tsedge_create_series(db, names[series_index]);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "tsedge_create_series: %s\n", tsedge_strerror(rc));
            free(batch);
            tsedge_close(db);
            return rc;
        }

        size_t series_points = points_for_series(config, series_index);
        for (size_t offset = 0; offset < series_points;) {
            size_t chunk = series_points - offset;
            if (chunk > batch_size) {
                chunk = batch_size;
            }
            for (size_t i = 0; i < chunk; ++i) {
                size_t local_index = offset + i;
                batch[i].timestamp = 1 + (int64_t)local_index;
                batch[i].value = sample_value(series_index, local_index);
            }
            rc = tsedge_append_batch(db, names[series_index], batch, chunk);
            if (rc != TSEDGE_OK) {
                fprintf(stderr, "tsedge_append_batch: %s\n", tsedge_strerror(rc));
                free(batch);
                tsedge_close(db);
                return rc;
            }
            offset += chunk;
        }
    }

    free(batch);
    rc = tsedge_flush_all(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_flush_all: %s\n", tsedge_strerror(rc));
        tsedge_close(db);
        return rc;
    }
    rc = tsedge_close(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_close: %s\n", tsedge_strerror(rc));
    }
    return rc;
}

static size_t scenario_default_range(read_scenario scenario, size_t total_points) {
    switch (scenario) {
        case SCENARIO_READ_RANGE_TINY:
        case SCENARIO_AGGREGATE_AVG_TINY:
        case SCENARIO_AGGREGATE_MIN_MAX_TINY:
            return total_points < 100u ? total_points : 100u;
        case SCENARIO_READ_RANGE_SMALL:
            return total_points < 1000u ? total_points : 1000u;
        case SCENARIO_READ_RANGE_MEDIUM:
        case SCENARIO_AGGREGATE_AVG_MEDIUM:
        case SCENARIO_AGGREGATE_MIN_MAX_MEDIUM:
            return total_points < 100000u ? total_points : 100000u;
        case SCENARIO_READ_RANGE_LARGE:
            return total_points < 500000u ? total_points : 500000u;
        case SCENARIO_READ_RANGE_FULL:
        case SCENARIO_AGGREGATE_AVG_FULL:
        case SCENARIO_AGGREGATE_MIN_MAX_FULL:
        case SCENARIO_WINDOW_AGGREGATE:
            return total_points;
        default:
            return total_points;
    }
}

static void choose_range(size_t series_points, size_t requested_range, int64_t* out_from, int64_t* out_to, size_t* out_count) {
    if (series_points == 0) {
        *out_from = 1;
        *out_to = 0;
        *out_count = 0;
        return;
    }
    size_t count = requested_range;
    if (count == 0 || count > series_points) {
        count = series_points;
    }
    size_t start = count == series_points ? 0u : (series_points - count) / 2u;
    *out_from = 1 + (int64_t)start;
    *out_to = *out_from + (int64_t)count - 1;
    *out_count = count;
}

static int read_cb(const tsedge_point* point, void* user_data) {
    read_accumulator* acc = (read_accumulator*)user_data;
    ++acc->points_read;
    acc->value_sum += point->value;
    return 0;
}

static int run_read_once(tsedge_db* db, char (*names)[64], const bench_config* config, read_scenario scenario, size_t range_size, scenario_result* result) {
    (void)scenario;
    read_accumulator acc;
    memset(&acc, 0, sizeof(acc));
    size_t expected_count = 0;

    double start = now_seconds();
    for (size_t series_index = 0; series_index < config->series_count; ++series_index) {
        int64_t from = 0;
        int64_t to = 0;
        size_t range_count = 0;
        choose_range(points_for_series(config, series_index), range_size, &from, &to, &range_count);
        expected_count += range_count;
        if (range_count == 0) {
            continue;
        }

        int rc = tsedge_read_range(db, names[series_index], from, to, read_cb, &acc);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "tsedge_read_range: %s\n", tsedge_strerror(rc));
            return rc;
        }
    }
    result->query_seconds += now_seconds() - start;
    result->points_read += acc.points_read;
    result->value_sum += acc.value_sum;
    result->result_count += expected_count;
    return TSEDGE_OK;
}

static int run_avg_once(tsedge_db* db, char (*names)[64], const bench_config* config, size_t range_size, scenario_result* result) {
    double weighted_sum = 0.0;
    size_t expected_count = 0;

    double start = now_seconds();
    for (size_t series_index = 0; series_index < config->series_count; ++series_index) {
        int64_t from = 0;
        int64_t to = 0;
        size_t range_count = 0;
        choose_range(points_for_series(config, series_index), range_size, &from, &to, &range_count);
        expected_count += range_count;
        if (range_count == 0) {
            continue;
        }

        double avg = 0.0;
        int rc = tsedge_aggregate(db, names[series_index], from, to, TSEDGE_AGG_AVG, &avg);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "tsedge_aggregate AVG: %s\n", tsedge_strerror(rc));
            return rc;
        }
        weighted_sum += avg * (double)range_count;
    }
    result->query_seconds += now_seconds() - start;
    result->result_count += expected_count;
    result->result_value += expected_count > 0 ? weighted_sum / (double)expected_count : 0.0;
    return TSEDGE_OK;
}

static int run_min_max_once(tsedge_db* db, char (*names)[64], const bench_config* config, size_t range_size, scenario_result* result) {
    int has_value = 0;
    double min_value = 0.0;
    double max_value = 0.0;
    size_t expected_count = 0;

    double start = now_seconds();
    for (size_t series_index = 0; series_index < config->series_count; ++series_index) {
        int64_t from = 0;
        int64_t to = 0;
        size_t range_count = 0;
        choose_range(points_for_series(config, series_index), range_size, &from, &to, &range_count);
        expected_count += range_count;
        if (range_count == 0) {
            continue;
        }

        double local_min = 0.0;
        double local_max = 0.0;
        tsedge_series* series = tsedge_db_find_series(db, names[series_index]);
        if (!series) {
            return TSEDGE_ERR_NOT_FOUND;
        }
        int rc = tsedge_series_aggregate_min_max(db, series, from, to, &local_min, &local_max);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "tsedge_aggregate MIN/MAX: %s\n", tsedge_strerror(rc));
            return rc;
        }
        if (!has_value || local_min < min_value) {
            min_value = local_min;
        }
        if (!has_value || local_max > max_value) {
            max_value = local_max;
        }
        has_value = 1;
    }
    result->query_seconds += now_seconds() - start;
    result->result_count += expected_count;
    result->result_min += min_value;
    result->result_max += max_value;
    return TSEDGE_OK;
}

static size_t choose_window_size(const bench_config* config, size_t series_points) {
    if (config->window_size != 0) {
        return config->window_size;
    }
    size_t target = config->target_windows == 0 ? 1000u : config->target_windows;
    size_t window_size = series_points / target;
    if (series_points % target != 0) {
        ++window_size;
    }
    return window_size == 0 ? 1u : window_size;
}

static int run_window_once(tsedge_db* db, char (*names)[64], const bench_config* config, scenario_result* result) {
    size_t source_points = 0;
    size_t total_windows = 0;
    size_t first_window_size = 0;
    double avg_sum = 0.0;

    double start = now_seconds();
    for (size_t series_index = 0; series_index < config->series_count; ++series_index) {
        size_t series_points = points_for_series(config, series_index);
        if (series_points == 0) {
            continue;
        }
        size_t window_size = choose_window_size(config, series_points);
        if (first_window_size == 0) {
            first_window_size = window_size;
        }

        tsedge_window_aggregate* windows = NULL;
        size_t window_count = 0;
        int rc = tsedge_aggregate_windowed(
            db,
            names[series_index],
            1,
            1 + (int64_t)series_points,
            (int64_t)window_size,
            &windows,
            &window_count
        );
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "tsedge_aggregate_windowed: %s\n", tsedge_strerror(rc));
            return rc;
        }
        for (size_t i = 0; i < window_count; ++i) {
            source_points += (size_t)windows[i].count;
            avg_sum += windows[i].avg;
            result->window_avg_sum += windows[i].avg;
            result->window_count_sum += windows[i].count;
        }
        if (window_count > 0) {
            if (!result->has_first_window) {
                result->first_window = windows[0];
                result->has_first_window = 1;
            }
            result->last_window = windows[window_count - 1u];
        }
        total_windows += window_count;
        tsedge_free_window_aggregates(windows);
    }

    result->query_seconds += now_seconds() - start;
    result->result_count += source_points;
    result->window_count += total_windows;
    result->window_size += first_window_size;
    result->result_value += total_windows > 0 ? avg_sum / (double)total_windows : 0.0;
    return TSEDGE_OK;
}

static int run_scenario(tsedge_db* db, char (*names)[64], const bench_config* config, read_scenario scenario) {
    size_t range_size = config->range_size != 0
        ? config->range_size
        : scenario_default_range(scenario, config->points);
    scenario_result result;
    memset(&result, 0, sizeof(result));

    for (size_t repeat = 0; repeat < config->repeat; ++repeat) {
        tsedge_debug_reset_read_stats(db);
        int rc = TSEDGE_OK;
        if (scenario_is_read(scenario)) {
            rc = run_read_once(db, names, config, scenario, range_size, &result);
        } else if (scenario_is_avg(scenario)) {
            rc = run_avg_once(db, names, config, range_size, &result);
        } else if (scenario_is_window(scenario)) {
            rc = run_window_once(db, names, config, &result);
        } else {
            rc = run_min_max_once(db, names, config, range_size, &result);
        }
        if (rc != TSEDGE_OK) {
            return rc;
        }

        tsedge_read_debug_stats stats;
        memset(&stats, 0, sizeof(stats));
        tsedge_debug_get_read_stats(db, &stats);
        result.stats.blocks_total += stats.blocks_total;
        result.stats.blocks_scanned += stats.blocks_scanned;
        result.stats.blocks_skipped += stats.blocks_skipped;
        result.stats.blocks_decoded += stats.blocks_decoded;
        result.stats.points_decoded += stats.points_decoded;
    }

    double repeats = (double)config->repeat;
    double query_seconds = result.query_seconds / repeats;
    size_t result_count = result.result_count / config->repeat;
    size_t points_read = result.points_read / config->repeat;
    size_t window_count = result.window_count / config->repeat;
    size_t window_size = result.window_size / config->repeat;
    double points_per_second = query_seconds > 0.0
        ? (double)(scenario_is_read(scenario) ? points_read : result_count) / query_seconds
        : 0.0;
    double windows_per_second = query_seconds > 0.0 ? (double)window_count / query_seconds : 0.0;

    printf("scenario: %s\n", scenario_name(scenario));
    printf("series_count: %zu\n", config->series_count);
    printf("total_points: %zu\n", config->points);
    printf("range_points: %zu\n", result_count);
    printf("repeat: %zu\n", config->repeat);
    printf("query_seconds: %.9f\n", query_seconds);
    printf("points_read: %zu\n", points_read);
    printf("points_per_second: %.2f\n", points_per_second);
    printf("aggregate: %s\n", aggregate_name(scenario));
    printf("result_count: %zu\n", result_count);
    printf("value_sum: %.6f\n", result.value_sum / repeats);
    if (scenario_is_window(scenario)) {
        printf("window_size: %zu\n", window_size);
        printf("target_windows: %zu\n", config->target_windows);
        printf("window_count: %zu\n", window_count);
        printf("windows_per_second: %.2f\n", windows_per_second);
        printf("source_points_per_second: %.2f\n", points_per_second);
        printf("avg_of_avgs: %.6f\n", result.result_value / repeats);
        printf("window_avg_sum: %.6f\n", result.window_avg_sum / repeats);
        printf("window_count_sum: %" PRIu64 "\n", result.window_count_sum / (uint64_t)config->repeat);
        if (result.has_first_window) {
            printf("first_window_start: %" PRId64 "\n", result.first_window.window_start);
            printf("first_window_end: %" PRId64 "\n", result.first_window.window_end);
            printf("first_window_count: %" PRIu64 "\n", result.first_window.count);
            printf("first_window_min: %.6f\n", result.first_window.min);
            printf("first_window_max: %.6f\n", result.first_window.max);
            printf("first_window_avg: %.6f\n", result.first_window.avg);
            printf("last_window_start: %" PRId64 "\n", result.last_window.window_start);
            printf("last_window_end: %" PRId64 "\n", result.last_window.window_end);
            printf("last_window_count: %" PRIu64 "\n", result.last_window.count);
            printf("last_window_min: %.6f\n", result.last_window.min);
            printf("last_window_max: %.6f\n", result.last_window.max);
            printf("last_window_avg: %.6f\n", result.last_window.avg);
        }
    } else if (scenario_is_avg(scenario)) {
        printf("result_value: %.6f\n", result.result_value / repeats);
    } else if (!scenario_is_read(scenario)) {
        printf("result_min: %.6f\n", result.result_min / repeats);
        printf("result_max: %.6f\n", result.result_max / repeats);
    }
    printf("blocks_total: %zu\n", result.stats.blocks_total / config->repeat);
    printf("blocks_scanned: %zu\n", result.stats.blocks_scanned / config->repeat);
    printf("blocks_skipped: %zu\n", result.stats.blocks_skipped / config->repeat);
    printf("blocks_decoded: %zu\n", result.stats.blocks_decoded / config->repeat);
    printf("points_decoded: %zu\n", result.stats.points_decoded / config->repeat);
    printf("db_path: %s\n", config->db_path);
    printf("\n");
    return TSEDGE_OK;
}

static int run_benchmark(const bench_config* config) {
    if (remove_tree(config->db_path) != 0) {
        fprintf(stderr, "failed to remove old database: %s\n", config->db_path);
        return 1;
    }

    char (*names)[64] = (char (*)[64])calloc(config->series_count, sizeof(*names));
    if (!names) {
        return 1;
    }

    int rc = generate_database(config, names);
    if (rc != TSEDGE_OK) {
        free(names);
        return 1;
    }

    tsedge_db* db = NULL;
    rc = tsedge_open(config->db_path, &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "tsedge_open after generate: %s\n", tsedge_strerror(rc));
        free(names);
        return 1;
    }

    if (config->scenario_all) {
        for (size_t i = 0; i < sizeof(all_scenarios) / sizeof(all_scenarios[0]); ++i) {
            rc = run_scenario(db, names, config, all_scenarios[i]);
            if (rc != TSEDGE_OK) {
                break;
            }
        }
    } else {
        rc = run_scenario(db, names, config, config->scenario);
    }

    int close_rc = tsedge_close(db);
    free(names);
    if (close_rc != TSEDGE_OK && rc == TSEDGE_OK) {
        rc = close_rc;
    }
    if (!config->keep_db) {
        (void)remove_tree(config->db_path);
    }
    return rc == TSEDGE_OK ? 0 : 1;
}

int main(int argc, char** argv) {
    bench_config config;
    if (!parse_args(argc, argv, &config)) {
        print_help(argv[0]);
        return 1;
    }
    return run_benchmark(&config);
}
