#include "ecopost_ops.h"
#include "ecopost_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sanitize_series_filename(const char* series_name, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    size_t pos = 0;
    if (series_name) {
        for (const unsigned char* p = (const unsigned char*)series_name; *p && pos + 5u < out_size; ++p) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-') {
                out[pos++] = (char)*p;
            } else if (*p == '.') {
                out[pos++] = '_';
            } else {
                out[pos++] = '_';
            }
        }
    }
    if (pos == 0) {
        snprintf(out, out_size, "series.csv");
        return;
    }
    snprintf(out + pos, out_size - pos, ".csv");
}

static void set_export_state(agent_state* state, int ok, const char* series_name, const char* filename, const char* path, size_t rows, const char* message) {
    state->export_last_run = 1;
    state->export_ok = ok;
    state->csv_ready = ok;
    snprintf(state->export_series, sizeof(state->export_series), "%s", series_name ? series_name : "");
    snprintf(state->last_csv_file, sizeof(state->last_csv_file), "%s", ok && filename ? filename : "");
    snprintf(state->export_path, sizeof(state->export_path), "%s", ok && path ? path : "");
    state->export_rows = rows;
    snprintf(state->export_message, sizeof(state->export_message), "%s", message ? message : "");
}

static int refresh_sensor_handles(agent_state* state) {
    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        int rc = tsedge_get_series_handle(state->db, SENSORS[i].name, &state->sensor_handles[i]);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    return TSEDGE_OK;
}

static int refresh_debug_handle(agent_state* state) {
    int rc = tsedge_get_series_handle(state->db, "debug.temp", &state->debug_handle);
    if (rc == TSEDGE_ERR_NOT_FOUND) {
        state->debug_handle = NULL;
        return TSEDGE_OK;
    }
    return rc;
}

int create_series(agent_state* state) {
    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        int rc = tsedge_create_series(state->db, SENSORS[i].name);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "create_series(%s): %s\n", SENSORS[i].name, tsedge_strerror(rc));
            return rc;
        }
    }
    int rc = refresh_sensor_handles(state);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    rc = refresh_debug_handle(state);
    if (rc != TSEDGE_OK) {
        return rc;
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
            int rc = tsedge_append_batch_handle(state->db, state->sensor_handles[sensor], points, chunk);
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

static int debug_series_exists(agent_state* state) {
    tsedge_series_stats stats;
    return tsedge_get_series_stats(state->db, "debug.temp", &stats) == TSEDGE_OK;
}

static double debug_value(size_t index) {
    return 20.0 + (double)(index % 60u) * 0.05;
}

static int append_debug_point_if_exists(agent_state* state, int64_t timestamp, size_t* added) {
    if (added) {
        *added = 0;
    }
    if (!debug_series_exists(state)) {
        state->debug_handle = NULL;
        return TSEDGE_OK;
    }
    if (!state->debug_handle) {
        int rc = refresh_debug_handle(state);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    int rc = tsedge_append_handle(state->db, state->debug_handle, timestamp, debug_value(state->tick));
    if (rc == TSEDGE_OK && added) {
        *added = 1u;
    }
    return rc;
}

static int append_one_step(agent_state* state, size_t* out_points_written) {
    int64_t timestamp = ecopost_simulated_timestamp(state);
    int pollution_active = state->pollution_ticks_left > 0;
    size_t points_written = SERIES_COUNT;

    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        double value = sensor_value(state->tick, i, pollution_active && i == 4u);
        int rc = tsedge_append_handle(state->db, state->sensor_handles[i], timestamp, value);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "append(%s): %s\n", SENSORS[i].name, tsedge_strerror(rc));
            return rc;
        }
        state->last_values[i] = value;
    }

    size_t debug_added = 0;
    int rc = append_debug_point_if_exists(state, timestamp, &debug_added);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    points_written += debug_added;

    if (state->pollution_ticks_left > 0) {
        --state->pollution_ticks_left;
    }
    ++state->tick;
    if (out_points_written) {
        *out_points_written = points_written;
    }
    return TSEDGE_OK;
}

int append_live_points(agent_state* state) {
    size_t points_written = 0;
    int rc = append_one_step(state, &points_written);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    add_event(state, "append", points_written > SERIES_COUNT ? "Записаны точки датчиков и debug.temp." : "Записано 6 точек датчиков.");
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

typedef struct {
    size_t count;
} range_count_context;

static int count_point_callback(const tsedge_point* point, void* user_data) {
    (void)point;
    range_count_context* ctx = (range_count_context*)user_data;
    ++ctx->count;
    return 0;
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
    state->debug_handle = NULL;
    return refresh_sensor_handles(state);
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

int export_csv(agent_state* state, const char* series_name) {
    if (!series_name || series_name[0] == '\0') {
        set_export_state(state, 0, "", NULL, NULL, 0, "Имя ряда пустое.");
        set_last_command(state, "export_csv", "error", "Имя ряда пустое.", 0);
        add_event(state, "export", "Не удалось выгрузить CSV: имя ряда пустое.");
        return TSEDGE_OK;
    }

    tsedge_series_stats stats;
    int rc = tsedge_get_series_stats(state->db, series_name, &stats);
    if (rc != TSEDGE_OK) {
        const char* message = rc == TSEDGE_ERR_NOT_FOUND ? "Ряд не найден." : "Некорректное имя ряда.";
        char command_message[192];
        snprintf(command_message, sizeof(command_message), "%s: %s", rc == TSEDGE_ERR_NOT_FOUND ? "Ряд не найден" : "Некорректное имя ряда", series_name);
        set_export_state(state, 0, series_name, NULL, NULL, 0, message);
        set_last_command(state, "export_csv", "error", command_message, 0);
        add_event(state, "export", rc == TSEDGE_ERR_NOT_FOUND ? "Не удалось выгрузить CSV: ряд не найден." : "Не удалось выгрузить CSV: имя ряда некорректно.");
        return TSEDGE_OK;
    }

    char filename[160];
    sanitize_series_filename(series_name, filename, sizeof(filename));
    char path[1024];
    path_join(path, sizeof(path), state->config.output_path, filename);
    int64_t to = ecopost_simulated_timestamp(state);
    rc = tsedge_flush(state->db, series_name);
    if (rc != TSEDGE_OK) {
        set_export_state(state, 0, series_name, NULL, NULL, 0, "Не удалось сбросить буфер.");
        set_last_command(state, "export_csv", "error", tsedge_strerror(rc), 0);
        return rc;
    }
    add_event(state, "buffer", "Буфер сброшен на диск.");

    range_count_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    rc = tsedge_read_range(state->db, series_name, START_TIMESTAMP, to, count_point_callback, &ctx);
    if (rc != TSEDGE_OK) {
        set_export_state(state, 0, series_name, NULL, NULL, 0, "Не удалось прочитать ряд.");
        set_last_command(state, "export_csv", "error", tsedge_strerror(rc), 0);
        return rc;
    }

    rc = tsedge_export_csv(state->db, series_name, START_TIMESTAMP, to, path);
    if (rc != TSEDGE_OK) {
        set_export_state(state, 0, series_name, NULL, NULL, 0, "CSV не создан.");
        set_last_command(state, "export_csv", "error", tsedge_strerror(rc), 0);
        return rc;
    }
    char message[192];
    snprintf(message, sizeof(message), "CSV выгружен: %s", series_name);
    set_export_state(state, 1, series_name, filename, path, ctx.count, "CSV выгружен.");
    set_last_command(state, "export_csv", "ok", message, ctx.count);
    add_event(state, "export", message);
    return TSEDGE_OK;
}

int append_steps(agent_state* state, size_t steps, const char* command_name, const char* message) {
    size_t affected = 0;
    for (size_t i = 0; i < steps; ++i) {
        size_t written = 0;
        int rc = append_one_step(state, &written);
        if (rc != TSEDGE_OK) {
            set_last_command(state, command_name, "error", tsedge_strerror(rc), affected);
            return rc;
        }
        affected += written;
    }
    char event_message[128];
    if (affected == steps * SERIES_COUNT) {
        snprintf(event_message, sizeof(event_message), "%s", message);
    } else {
        snprintf(event_message, sizeof(event_message), "Добавлено %zu точек, включая debug.temp.", affected);
    }
    set_last_command(state, command_name, "ok", event_message, affected);
    add_event(state, "append", event_message);
    return TSEDGE_OK;
}

int append_custom_points(agent_state* state, int64_t requested_points) {
    if (requested_points <= 0 || requested_points > 1000000LL) {
        set_last_command(state, "append_custom", "error", "Укажите от 1 до 1 000 000 точек.", 0);
        return TSEDGE_OK;
    }

    size_t target = (size_t)requested_points;
    size_t affected = 0;
    int debug_available = debug_series_exists(state);
    if (debug_available && !state->debug_handle) {
        int rc = refresh_debug_handle(state);
        if (rc != TSEDGE_OK) {
            set_last_command(state, "append_custom", "error", tsedge_strerror(rc), affected);
            return rc;
        }
    }

    while (affected < target) {
        int64_t timestamp = ecopost_simulated_timestamp(state);
        int pollution_active = state->pollution_ticks_left > 0;
        int wrote_pm25 = 0;

        for (size_t sensor = 0; sensor < SERIES_COUNT && affected < target; ++sensor) {
            double value = sensor_value(state->tick, sensor, pollution_active && sensor == 4u);
            int rc = tsedge_append_handle(state->db, state->sensor_handles[sensor], timestamp, value);
            if (rc != TSEDGE_OK) {
                set_last_command(state, "append_custom", "error", tsedge_strerror(rc), affected);
                return rc;
            }
            state->last_values[sensor] = value;
            wrote_pm25 = wrote_pm25 || sensor == 4u;
            ++affected;
        }

        if (debug_available && affected < target) {
            int rc = tsedge_append_handle(state->db, state->debug_handle, timestamp, debug_value(state->tick));
            if (rc != TSEDGE_OK) {
                set_last_command(state, "append_custom", "error", tsedge_strerror(rc), affected);
                return rc;
            }
            ++affected;
        }

        if (state->pollution_ticks_left > 0 && wrote_pm25) {
            --state->pollution_ticks_left;
        }
        ++state->tick;
    }

    char message[128];
    snprintf(message, sizeof(message), "Добавлено %zu точек.", affected);
    set_last_command(state, "append_custom", "ok", message, affected);
    add_event(state, "append", message);
    add_event(state, "wal", "Точки записаны в WAL.");
    add_event(state, "buffer", "Точки добавлены в буфер.");
    return TSEDGE_OK;
}

int append_batch_command(agent_state* state) {
    tsedge_point* points = (tsedge_point*)malloc(DIAG_BATCH_SIZE * sizeof(*points));
    if (!points) {
        set_last_command(state, "append_batch", "error", "Нет памяти для batch-записи.", 0);
        return TSEDGE_ERR_NO_MEMORY;
    }

    size_t start_tick = state->tick;
    size_t affected = DIAG_BATCH_SIZE * SERIES_COUNT;
    for (size_t sensor = 0; sensor < SERIES_COUNT; ++sensor) {
        for (size_t j = 0; j < DIAG_BATCH_SIZE; ++j) {
            size_t index = start_tick + j;
            points[j].timestamp = START_TIMESTAMP + (int64_t)index * STEP_MS;
            points[j].value = sensor_value(index, sensor, state->pollution_ticks_left > 0 && sensor == 4u);
        }
        int rc = tsedge_append_batch_handle(state->db, state->sensor_handles[sensor], points, DIAG_BATCH_SIZE);
        if (rc != TSEDGE_OK) {
            free(points);
            set_last_command(state, "append_batch", "error", tsedge_strerror(rc), sensor * DIAG_BATCH_SIZE);
            return rc;
        }
        state->last_values[sensor] = points[DIAG_BATCH_SIZE - 1u].value;
    }

    if (debug_series_exists(state)) {
        for (size_t j = 0; j < DIAG_BATCH_SIZE; ++j) {
            size_t index = start_tick + j;
            points[j].timestamp = START_TIMESTAMP + (int64_t)index * STEP_MS;
            points[j].value = debug_value(index);
        }
        if (!state->debug_handle) {
            int handle_rc = refresh_debug_handle(state);
            if (handle_rc != TSEDGE_OK) {
                free(points);
                set_last_command(state, "append_batch", "error", tsedge_strerror(handle_rc), affected);
                return handle_rc;
            }
        }
        int rc = tsedge_append_batch_handle(state->db, state->debug_handle, points, DIAG_BATCH_SIZE);
        if (rc != TSEDGE_OK) {
            free(points);
            set_last_command(state, "append_batch", "error", tsedge_strerror(rc), affected);
            return rc;
        }
        affected += DIAG_BATCH_SIZE;
    }

    free(points);
    state->tick += DIAG_BATCH_SIZE;
    set_last_command(state, "append_batch", "ok", affected > DIAG_BATCH_SIZE * SERIES_COUNT ? "Batch-запись выполнена, включая debug.temp." : "Batch-запись выполнена.", affected);
    add_event(state, "append", affected > DIAG_BATCH_SIZE * SERIES_COUNT ? "Batch-запись выполнена, включая debug.temp." : "Batch-запись выполнена.");
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

int aggregate_avg_command(agent_state* state, const char* series_name) {
    const char* selected_series = series_name && series_name[0] ? series_name : "air.temperature";
    int64_t to = ecopost_simulated_timestamp(state);
    int64_t from = to - 60LL * 60LL * 1000LL;
    if (from < START_TIMESTAMP) {
        from = START_TIMESTAMP;
    }

    double result = 0.0;
    double started = monotonic_ms();
    int rc = tsedge_aggregate(state->db, selected_series, from, to, TSEDGE_AGG_AVG, &result);
    double duration = monotonic_ms() - started;
    set_last_query_base(state, "AVG", selected_series, from, to);
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

int aggregate_min_max_command(agent_state* state, const char* series_name) {
    const char* selected_series = series_name && series_name[0] ? series_name : "pm25.concentration";
    int64_t to = ecopost_simulated_timestamp(state);
    int64_t from = to - 60LL * 60LL * 1000LL;
    if (from < START_TIMESTAMP) {
        from = START_TIMESTAMP;
    }

    double min_value = 0.0;
    double max_value = 0.0;
    double started = monotonic_ms();
    int rc = tsedge_aggregate(state->db, selected_series, from, to, TSEDGE_AGG_MIN, &min_value);
    if (rc == TSEDGE_OK) {
        rc = tsedge_aggregate(state->db, selected_series, from, to, TSEDGE_AGG_MAX, &max_value);
    }
    double duration = monotonic_ms() - started;
    set_last_query_base(state, "MIN_MAX", selected_series, from, to);
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

int window_aggregate_command(agent_state* state, const char* series_name, int64_t requested_window_size, int has_window_size) {
    const char* selected_series = series_name && series_name[0] ? series_name : "air.temperature";
    int64_t window_size = has_window_size ? requested_window_size : 1000LL;

    memset(&state->window_aggregate, 0, sizeof(state->window_aggregate));
    state->window_aggregate.last_run = 1;
    snprintf(state->window_aggregate.series, sizeof(state->window_aggregate.series), "%s", selected_series);
    state->window_aggregate.window_size = window_size;

    if (window_size <= 0) {
        snprintf(state->window_aggregate.message, sizeof(state->window_aggregate.message), "Размер окна должен быть больше нуля.");
        set_last_query_base(state, "WINDOW", selected_series, 0, 0);
        snprintf(state->last_query.status, sizeof(state->last_query.status), "error");
        set_last_command(state, "window_aggregate", "error", "Размер окна должен быть больше нуля.", 0);
        add_event(state, "query", "Агрегация по окнам не выполнена: некорректный размер окна.");
        return TSEDGE_OK;
    }

    tsedge_series_stats stats;
    int stats_rc = tsedge_get_series_stats(state->db, selected_series, &stats);
    if (stats_rc != TSEDGE_OK) {
        snprintf(state->window_aggregate.message, sizeof(state->window_aggregate.message), "%s", tsedge_strerror(stats_rc));
        set_last_query_base(state, "WINDOW", selected_series, 0, 0);
        snprintf(state->last_query.status, sizeof(state->last_query.status), "error");
        set_last_command(state, "window_aggregate", "error", tsedge_strerror(stats_rc), 0);
        add_event(state, "query", "Агрегация по окнам не выполнена: ряд не найден.");
        return stats_rc == TSEDGE_ERR_NOT_FOUND ? TSEDGE_OK : stats_rc;
    }
    if (!stats.has_time_range) {
        snprintf(state->window_aggregate.message, sizeof(state->window_aggregate.message), "В ряду нет точек.");
        set_last_query_base(state, "WINDOW", selected_series, 0, 0);
        set_last_command(state, "window_aggregate", "error", "В ряду нет точек.", 0);
        add_event(state, "query", "Агрегация по окнам не выполнена: в ряду нет точек.");
        return TSEDGE_OK;
    }
    if (stats.max_timestamp == INT64_MAX) {
        snprintf(state->window_aggregate.message, sizeof(state->window_aggregate.message), "Диапазон времени слишком большой.");
        set_last_query_base(state, "WINDOW", selected_series, stats.min_timestamp, stats.max_timestamp);
        snprintf(state->last_query.status, sizeof(state->last_query.status), "error");
        set_last_command(state, "window_aggregate", "error", "Диапазон времени слишком большой.", 0);
        return TSEDGE_OK;
    }

    int64_t from = stats.min_timestamp;
    int64_t to_exclusive = stats.max_timestamp + 1LL;

    tsedge_window_aggregate* windows = NULL;
    size_t window_count = 0;
    double started = monotonic_ms();
    int rc = tsedge_aggregate_windowed(state->db, selected_series, from, to_exclusive, window_size, &windows, &window_count);
    double duration = monotonic_ms() - started;

    state->window_aggregate.ok = rc == TSEDGE_OK;
    state->window_aggregate.query_seconds = duration / 1000.0;

    set_last_query_base(state, "WINDOW", selected_series, from, to_exclusive);
    state->last_query.duration_ms = duration;

    if (rc != TSEDGE_OK) {
        snprintf(state->window_aggregate.message, sizeof(state->window_aggregate.message), "%s", tsedge_strerror(rc));
        snprintf(state->last_query.status, sizeof(state->last_query.status), "error");
        set_last_command(state, "window_aggregate", "error", tsedge_strerror(rc), 0);
        tsedge_free_window_aggregates(windows);
        return rc == TSEDGE_ERR_NOT_FOUND ? TSEDGE_OK : rc;
    }

    size_t source_points = 0;
    double min_value = 0.0;
    double max_value = 0.0;
    double avg_sum = 0.0;
    double weighted_sum = 0.0;
    for (size_t i = 0; i < window_count; ++i) {
        source_points += (size_t)windows[i].count;
        avg_sum += windows[i].avg;
        weighted_sum += windows[i].avg * (double)windows[i].count;
        if (i == 0 || windows[i].min < min_value) {
            min_value = windows[i].min;
        }
        if (i == 0 || windows[i].max > max_value) {
            max_value = windows[i].max;
        }
    }

    state->window_aggregate.window_count = window_count;
    state->window_aggregate.source_points_estimate = source_points;
    state->window_aggregate.downsample_ratio = window_count > 0 ? (double)source_points / (double)window_count : 0.0;
    state->window_aggregate.min_value = min_value;
    state->window_aggregate.max_value = max_value;
    state->window_aggregate.avg_of_avgs = window_count > 0 ? avg_sum / (double)window_count : 0.0;
    state->window_aggregate.weighted_avg = source_points > 0 ? weighted_sum / (double)source_points : 0.0;
    if (window_count > 0) {
        state->window_aggregate.first = windows[0];
        state->window_aggregate.last = windows[window_count - 1u];
    }
    snprintf(state->window_aggregate.message, sizeof(state->window_aggregate.message), "Агрегация по окнам выполнена.");

    state->last_query.points_read = source_points;
    state->last_query.result = (double)window_count;
    state->last_query.min_value = min_value;
    state->last_query.max_value = max_value;
    set_last_command(state, "window_aggregate", "ok", "Агрегация по окнам выполнена.", source_points);
    char event_message[192];
    snprintf(event_message, sizeof(event_message), "Агрегация по окнам выполнена: %s.", selected_series);
    add_event(state, "query", event_message);

    tsedge_free_window_aggregates(windows);
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

int create_debug_series(agent_state* state) {
    const char* name = "debug.temp";
    int existed = debug_series_exists(state);
    int rc = tsedge_create_series(state->db, name);
    if (rc != TSEDGE_OK) {
        set_last_command(state, "create_debug_series", "error", tsedge_strerror(rc), 0);
        return rc;
    }
    rc = tsedge_get_series_handle(state->db, name, &state->debug_handle);
    if (rc != TSEDGE_OK) {
        set_last_command(state, "create_debug_series", "error", tsedge_strerror(rc), 0);
        return rc;
    }

    int64_t timestamp = ecopost_simulated_timestamp(state);
    for (size_t i = 0; i < 8u; ++i) {
        rc = tsedge_append_handle(state->db, state->debug_handle, timestamp + (int64_t)i * STEP_MS, debug_value(state->tick + i));
        if (rc != TSEDGE_OK) {
            set_last_command(state, "create_debug_series", "error", tsedge_strerror(rc), i);
            return rc;
        }
    }

    set_last_command(state, "create_debug_series", "ok", existed ? "В debug.temp добавлено 8 точек." : "Тестовый ряд создан: 8 точек.", 8u);
    add_event(state, "append", existed ? "В debug.temp добавлено 8 точек." : "Тестовый ряд debug.temp создан.");
    return TSEDGE_OK;
}

int delete_debug_series(agent_state* state) {
    int rc = tsedge_delete_series(state->db, "debug.temp");
    if (rc == TSEDGE_ERR_NOT_FOUND) {
        set_last_command(state, "delete_debug_series", "error", "Тестовый ряд не найден.", 0);
        add_event(state, "delete", "Тестовый ряд не найден.");
        return TSEDGE_OK;
    }
    if (rc != TSEDGE_OK) {
        set_last_command(state, "delete_debug_series", "error", tsedge_strerror(rc), 0);
        return rc;
    }
    state->debug_handle = NULL;
    set_last_command(state, "delete_debug_series", "ok", "Тестовый ряд удалён.", 0);
    add_event(state, "delete", "Тестовый ряд удалён.");
    return TSEDGE_OK;
}

int reset_demo(agent_state* state) {
    if (state->db) {
        tsedge_close(state->db);
        state->db = NULL;
    }
    memset(state->sensor_handles, 0, sizeof(state->sensor_handles));
    state->debug_handle = NULL;
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
    state->export_last_run = 0;
    state->export_ok = 0;
    state->export_series[0] = '\0';
    state->export_path[0] = '\0';
    state->export_rows = 0;
    state->export_message[0] = '\0';
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
