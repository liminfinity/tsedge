#include "ecopost_ops.h"
#include "ecopost_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int create_series(agent_state* state) {
    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        int rc = tsedge_create_series(state->db, SENSORS[i].name);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "create_series(%s): %s\n", SENSORS[i].name, tsedge_strerror(rc));
            return rc;
        }
    }
    add_event(state, "sensor", "Созданы ряды экологического поста.");
    return TSEDGE_OK;
}

int append_history(agent_state* state) {
    tsedge_point* points = (tsedge_point*)malloc(BATCH_SIZE * sizeof(*points));
    if (!points) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    for (size_t sensor = 0; sensor < SERIES_COUNT; ++sensor) {
        for (size_t offset = 0; offset < HISTORY_POINTS;) {
            size_t chunk = HISTORY_POINTS - offset;
            if (chunk > BATCH_SIZE) {
                chunk = BATCH_SIZE;
            }
            for (size_t j = 0; j < chunk; ++j) {
                size_t index = offset + j;
                points[j].timestamp = START_TIMESTAMP + (int64_t)index * STEP_MS;
                points[j].value = sensor_value(index, sensor, 0);
            }
            int rc = tsedge_append_batch(state->db, SENSORS[sensor].name, points, chunk);
            if (rc != TSEDGE_OK) {
                free(points);
                return rc;
            }
            offset += chunk;
        }
    }

    free(points);
    state->tick = HISTORY_POINTS;
    add_event(state, "append", "Подготовлена история, чтобы были видны segment-файлы.");
    return TSEDGE_OK;
}

static int append_one_step(agent_state* state) {
    int64_t timestamp = ecopost_simulated_timestamp(state);
    int pollution_active = state->pollution_ticks_left > 0;

    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        double value = sensor_value(state->tick, i, pollution_active && i == 4u);
        int rc = tsedge_append(state->db, SENSORS[i].name, timestamp, value);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "append(%s): %s\n", SENSORS[i].name, tsedge_strerror(rc));
            return rc;
        }
        state->last_values[i] = value;
    }

    if (state->pollution_ticks_left > 0) {
        --state->pollution_ticks_left;
    }
    ++state->tick;
    return TSEDGE_OK;
}

int append_live_points(agent_state* state) {
    int rc = append_one_step(state);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    add_event(state, "append", "Получено 6 новых точек от датчиков.");
    add_event(state, "wal", "Точки записаны в WAL.");
    add_event(state, "buffer", "Точки добавлены в буфер.");
    return TSEDGE_OK;
}

int get_primary_stats(agent_state* state, tsedge_series_stats* stats) {
    return tsedge_get_series_stats(state->db, "air.temperature", stats);
}

size_t total_segments(agent_state* state) {
    size_t total = 0;
    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        tsedge_series_stats stats;
        if (tsedge_get_series_stats(state->db, SENSORS[i].name, &stats) == TSEDGE_OK) {
            total += stats.segment_count;
        }
    }
    return total;
}

storage_totals collect_storage_totals(agent_state* state) {
    storage_totals totals;
    memset(&totals, 0, sizeof(totals));
    uint64_t total_points = 0;

    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        tsedge_series_stats stats;
        if (tsedge_get_series_stats(state->db, SENSORS[i].name, &stats) == TSEDGE_OK) {
            totals.raw_size_estimate_bytes += stats.raw_size_estimate_bytes;
            totals.compressed_size_bytes += stats.compressed_size_bytes;
            total_points += (uint64_t)(stats.total_indexed_points + stats.buffered_points);
        }
    }

    totals.compression_ratio = totals.compressed_size_bytes > 0
        ? (double)totals.raw_size_estimate_bytes / (double)totals.compressed_size_bytes
        : 0.0;
    totals.bytes_per_point = total_points > 0
        ? (double)totals.compressed_size_bytes / (double)total_points
        : 0.0;
    return totals;
}

int reopen_db(agent_state* state) {
    int rc = tsedge_close(state->db);
    state->db = NULL;
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = tsedge_open(state->config.db_path, &state->db);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return TSEDGE_OK;
}

int run_retention(agent_state* state) {
    size_t before = total_segments(state);
    int64_t threshold = START_TIMESTAMP + (int64_t)(state->tick > 64u ? state->tick - 64u : state->tick / 2u) * STEP_MS;
    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        int rc = tsedge_delete_before(state->db, SENSORS[i].name, threshold);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    size_t after = total_segments(state);
    state->retention_last_run = 1;
    state->retention_deleted_count = before > after ? before - after : 0u;
    snprintf(state->retention_last_deleted, sizeof(state->retention_last_deleted), "старые segment-файлы");
    set_last_command(state, "run_retention", "ok", state->retention_deleted_count ? "Очистка старых данных выполнена." : "Полных старых segment-файлов нет.", state->retention_deleted_count);
    add_event(state, "retention", state->retention_deleted_count ? "Retention удалил старые segment-файлы." : "Retention запущен, но полных старых segment-файлов нет.");
    return TSEDGE_OK;
}

int export_csv(agent_state* state) {
    char path[1024];
    path_join(path, sizeof(path), state->config.output_path, "ecopost_temperature.csv");
    int64_t to = ecopost_simulated_timestamp(state);
    int rc = tsedge_flush_all(state->db);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    add_event(state, "buffer", "Буфер сброшен на диск.");

    rc = tsedge_export_csv(state->db, "air.temperature", START_TIMESTAMP, to, path);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    state->csv_ready = 1;
    snprintf(state->last_csv_file, sizeof(state->last_csv_file), "ecopost_temperature.csv");
    set_last_command(state, "export_csv", "ok", "CSV-файл готов.", 0);
    add_event(state, "export", "CSV-файл готов для выгрузки.");
    return TSEDGE_OK;
}

int append_steps(agent_state* state, size_t steps, const char* command_name, const char* message) {
    for (size_t i = 0; i < steps; ++i) {
        int rc = append_one_step(state);
        if (rc != TSEDGE_OK) {
            set_last_command(state, command_name, "error", tsedge_strerror(rc), i * SERIES_COUNT);
            return rc;
        }
    }
    size_t affected = steps * SERIES_COUNT;
    set_last_command(state, command_name, "ok", message, affected);
    add_event(state, "append", message);
    return TSEDGE_OK;
}

int append_batch_command(agent_state* state) {
    tsedge_point* points = (tsedge_point*)malloc(DIAG_BATCH_SIZE * sizeof(*points));
    if (!points) {
        set_last_command(state, "append_batch", "error", "Нет памяти для batch-записи.", 0);
        return TSEDGE_ERR_NO_MEMORY;
    }

    size_t start_tick = state->tick;
    for (size_t sensor = 0; sensor < SERIES_COUNT; ++sensor) {
        for (size_t j = 0; j < DIAG_BATCH_SIZE; ++j) {
            size_t index = start_tick + j;
            points[j].timestamp = START_TIMESTAMP + (int64_t)index * STEP_MS;
            points[j].value = sensor_value(index, sensor, state->pollution_ticks_left > 0 && sensor == 4u);
        }
        int rc = tsedge_append_batch(state->db, SENSORS[sensor].name, points, DIAG_BATCH_SIZE);
        if (rc != TSEDGE_OK) {
            free(points);
            set_last_command(state, "append_batch", "error", tsedge_strerror(rc), sensor * DIAG_BATCH_SIZE);
            return rc;
        }
        state->last_values[sensor] = points[DIAG_BATCH_SIZE - 1u].value;
    }

    free(points);
    state->tick += DIAG_BATCH_SIZE;
    size_t affected = DIAG_BATCH_SIZE * SERIES_COUNT;
    set_last_command(state, "append_batch", "ok", "Batch-запись выполнена.", affected);
    add_event(state, "append", "Batch-запись выполнена.");
    return TSEDGE_OK;
}

int flush_all_command(agent_state* state) {
    int rc = tsedge_flush_all(state->db);
    if (rc != TSEDGE_OK) {
        set_last_command(state, "flush_all", "error", tsedge_strerror(rc), 0);
        return rc;
    }
    set_last_command(state, "flush_all", "ok", "Буфер сброшен на диск.", 0);
    add_event(state, "buffer", "Буфер сброшен на диск.");
    return TSEDGE_OK;
}

typedef struct {
    size_t count;
} range_count_context;

static int count_point_callback(const tsedge_point* point, void* user_data) {
    (void)point;
    range_count_context* ctx = (range_count_context*)user_data;
    ++ctx->count;
    return 0;
}

int read_last_range_command(agent_state* state) {
    int64_t to = ecopost_simulated_timestamp(state);
    int64_t from = to - 10LL * 60LL * 1000LL;
    if (from < START_TIMESTAMP) {
        from = START_TIMESTAMP;
    }

    range_count_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    double started = monotonic_ms();
    int rc = tsedge_read_range(state->db, "air.temperature", from, to, count_point_callback, &ctx);
    double duration = monotonic_ms() - started;
    set_last_query_base(state, "READ_RANGE", "air.temperature", from, to);
    state->last_query.points_read = ctx.count;
    state->last_query.duration_ms = duration;
    if (rc != TSEDGE_OK) {
        snprintf(state->last_query.status, sizeof(state->last_query.status), "error");
        set_last_command(state, "read_last_range", "error", tsedge_strerror(rc), 0);
        return rc;
    }
    set_last_command(state, "read_last_range", "ok", "Последний диапазон прочитан.", ctx.count);
    add_event(state, "query", "Последний диапазон прочитан.");
    return TSEDGE_OK;
}

int aggregate_avg_command(agent_state* state) {
    int64_t to = ecopost_simulated_timestamp(state);
    int64_t from = to - 60LL * 60LL * 1000LL;
    if (from < START_TIMESTAMP) {
        from = START_TIMESTAMP;
    }

    double result = 0.0;
    double started = monotonic_ms();
    int rc = tsedge_aggregate(state->db, "air.temperature", from, to, TSEDGE_AGG_AVG, &result);
    double duration = monotonic_ms() - started;
    set_last_query_base(state, "AVG", "air.temperature", from, to);
    state->last_query.result = result;
    state->last_query.duration_ms = duration;
    state->last_query.points_read = (size_t)((to - from) / STEP_MS);
    if (rc != TSEDGE_OK) {
        snprintf(state->last_query.status, sizeof(state->last_query.status), "error");
        set_last_command(state, "aggregate_avg", "error", tsedge_strerror(rc), 0);
        return rc;
    }
    set_last_command(state, "aggregate_avg", "ok", "AVG рассчитан.", 0);
    add_event(state, "query", "AVG рассчитан.");
    return TSEDGE_OK;
}

int aggregate_min_max_command(agent_state* state) {
    int64_t to = ecopost_simulated_timestamp(state);
    int64_t from = to - 60LL * 60LL * 1000LL;
    if (from < START_TIMESTAMP) {
        from = START_TIMESTAMP;
    }

    double min_value = 0.0;
    double max_value = 0.0;
    double started = monotonic_ms();
    int rc = tsedge_aggregate(state->db, "pm25.concentration", from, to, TSEDGE_AGG_MIN, &min_value);
    if (rc == TSEDGE_OK) {
        rc = tsedge_aggregate(state->db, "pm25.concentration", from, to, TSEDGE_AGG_MAX, &max_value);
    }
    double duration = monotonic_ms() - started;
    set_last_query_base(state, "MIN_MAX", "pm25.concentration", from, to);
    state->last_query.min_value = min_value;
    state->last_query.max_value = max_value;
    state->last_query.duration_ms = duration;
    state->last_query.points_read = (size_t)((to - from) / STEP_MS);
    if (rc != TSEDGE_OK) {
        snprintf(state->last_query.status, sizeof(state->last_query.status), "error");
        set_last_command(state, "aggregate_min_max", "error", tsedge_strerror(rc), 0);
        return rc;
    }
    set_last_command(state, "aggregate_min_max", "ok", "MIN/MAX рассчитаны.", 0);
    add_event(state, "query", "MIN/MAX рассчитаны.");
    return TSEDGE_OK;
}

int verify_db(agent_state* state) {
    tsedge_verify_report report;
    int rc = tsedge_verify(state->config.db_path, &report);
    state->verify_last_run = 1;
    state->verify_ok = rc == TSEDGE_OK;
    state->verify_report = report;
    if (rc == TSEDGE_OK) {
        set_last_command(state, "verify_db", "ok", "Проверка базы выполнена.", 0);
        add_event(state, "verify", "Проверка базы выполнена.");
        return TSEDGE_OK;
    }
    set_last_command(state, "verify_db", "error", "Проверка базы нашла ошибку.", 0);
    add_event(state, "verify", "Проверка базы нашла ошибку.");
    return rc == TSEDGE_ERR_CORRUPT ? TSEDGE_OK : rc;
}

int reset_demo(agent_state* state) {
    if (state->db) {
        tsedge_close(state->db);
        state->db = NULL;
    }
    if (remove_tree(state->config.db_path) != 0 || remove_tree(state->config.output_path) != 0) {
        return TSEDGE_ERR_IO;
    }
    if (make_dir(state->config.output_path) != 0) {
        return TSEDGE_ERR_IO;
    }
    memset(state->last_values, 0, sizeof(state->last_values));
    state->tick = 0;
    state->paused = 0;
    state->crashed = 0;
    state->network_online = 1;
    state->pollution_ticks_left = 0;
    state->crash_simulated = 0;
    state->wal_replayed = 0;
    state->recovered_points = 0;
    state->csv_ready = 0;
    state->last_csv_file[0] = '\0';
    state->retention_last_run = 0;
    state->retention_deleted_count = 0;
    state->retention_last_deleted[0] = '\0';
    state->verify_last_run = 0;
    state->verify_ok = 0;
    memset(&state->verify_report, 0, sizeof(state->verify_report));
    memset(&state->last_command, 0, sizeof(state->last_command));
    clear_last_query(state);
    state->event_count = 0;
    state->event_start = 0;

    int rc = tsedge_open(state->config.db_path, &state->db);
    if (rc == TSEDGE_OK) {
        rc = create_series(state);
    }
    if (rc == TSEDGE_OK) {
        rc = append_history(state);
    }
    return rc;
}
