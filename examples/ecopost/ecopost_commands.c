#include "ecopost_commands.h"
#include "ecopost_fs.h"
#include "ecopost_ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int parse_int64_field(const char* json, const char* field, int64_t* out_value) {
    char key[64];
    snprintf(key, sizeof(key), "\"%s\"", field);
    char* p = strstr(json, key);
    if (!p) {
        return 0;
    }
    p = strchr(p, ':');
    if (!p) {
        return 0;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }
    char* end = NULL;
    long long value = strtoll(p, &end, 10);
    if (end == p) {
        return 0;
    }
    *out_value = (int64_t)value;
    return 1;
}

static int parse_string_field(const char* json, const char* field, char* out, size_t out_size) {
    out[0] = '\0';
    char key[64];
    snprintf(key, sizeof(key), "\"%s\"", field);
    char* p = strstr(json, key);
    if (!p) {
        return 0;
    }
    p = strchr(p, ':');
    if (!p) {
        return 0;
    }
    p = strchr(p, '"');
    if (!p) {
        return 0;
    }
    ++p;
    char* end = strchr(p, '"');
    if (!end) {
        return 0;
    }
    size_t len = (size_t)(end - p);
    if (len >= out_size) {
        len = out_size - 1u;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

int read_command(agent_state* state, agent_command* command) {
    char path[1024];
    path_join(path, sizeof(path), state->config.output_path, "command.json");
    FILE* f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    unlink(path);

    memset(command, 0, sizeof(*command));
    if (!parse_string_field(buf, "command", command->command, sizeof(command->command))) {
        return 0;
    }
    parse_string_field(buf, "series", command->series, sizeof(command->series));
    command->window_size_set = parse_int64_field(buf, "window_size", &command->window_size);
    command->points_set = parse_int64_field(buf, "points", &command->points);
    return 1;
}

int handle_command(agent_state* state, const agent_command* request) {
    const char* command = request->command;
    if (strcmp(command, "network_offline") == 0) {
        state->network_online = 0;
        set_last_command(state, command, "ok", "Связь потеряна.", 0);
        add_event(state, "network", "Связь потеряна. Запись продолжается локально.");
    } else if (strcmp(command, "network_online") == 0) {
        state->network_online = 1;
        set_last_command(state, command, "ok", "Связь восстановлена.", 0);
        add_event(state, "network", "Связь восстановлена. Данные можно выгрузить.");
    } else if (strcmp(command, "simulate_pollution") == 0) {
        state->pollution_ticks_left = 90;
        set_last_command(state, command, "ok", "Всплеск PM2.5 включён.", 0);
        add_event(state, "pollution", "Зафиксирован всплеск PM2.5.");
    } else if (strcmp(command, "simulate_crash") == 0) {
        state->crashed = 1;
        state->paused = 1;
        state->crash_simulated = 1;
        set_last_command(state, command, "ok", "Сбой смоделирован.", 0);
        add_event(state, "recovery", "Сбой питания. Запись временно остановлена.");
    } else if (strcmp(command, "recover_from_wal") == 0) {
        int rc = reopen_db(state);
        if (rc != TSEDGE_OK) {
            set_last_command(state, command, "error", tsedge_strerror(rc), 0);
            return rc;
        }
        state->crashed = 0;
        state->paused = 0;
        state->wal_replayed = 1;
        state->recovered_points = 6u;
        set_last_command(state, command, "ok", "WAL replay выполнен.", state->recovered_points);
        add_event(state, "recovery", "Данные восстановлены из WAL.");
    } else if (strcmp(command, "append_one") == 0) {
        return append_steps(state, 1u, command, "Добавлено 6 точек.");
    } else if (strcmp(command, "append_100") == 0) {
        return append_steps(state, 100u, command, "Добавлено 600 точек.");
    } else if (strcmp(command, "append_1000") == 0) {
        return append_steps(state, 1000u, command, "Добавлено 6000 точек.");
    } else if (strcmp(command, "append_custom") == 0) {
        return append_custom_points(state, request->points_set ? request->points : 10000LL);
    } else if (strcmp(command, "append_batch") == 0) {
        return append_batch_command(state);
    } else if (strcmp(command, "flush_all") == 0) {
        return flush_all_command(state);
    } else if (strcmp(command, "read_last_range") == 0) {
        return read_last_range_command(state);
    } else if (strcmp(command, "aggregate_avg") == 0) {
        return aggregate_avg_command(state, request->series[0] ? request->series : "air.temperature");
    } else if (strcmp(command, "aggregate_min_max") == 0) {
        return aggregate_min_max_command(state, request->series[0] ? request->series : "pm25.concentration");
    } else if (strcmp(command, "window_aggregate") == 0) {
        return window_aggregate_command(state, request->series[0] ? request->series : "air.temperature", request->window_size, request->window_size_set);
    } else if (strcmp(command, "run_retention") == 0) {
        return run_retention(state);
    } else if (strcmp(command, "export_csv") == 0) {
        if (!state->network_online) {
            set_last_command(state, command, "error", "CSV нельзя выгрузить без связи.", 0);
            add_event(state, "export", "CSV нельзя выгрузить без связи.");
            return TSEDGE_OK;
        }
        return export_csv(state, request->series[0] ? request->series : "air.temperature");
    } else if (strcmp(command, "verify_db") == 0) {
        return verify_db(state);
    } else if (strcmp(command, "create_debug_series") == 0) {
        return create_debug_series(state);
    } else if (strcmp(command, "delete_debug_series") == 0) {
        return delete_debug_series(state);
    } else if (strcmp(command, "reset_demo") == 0) {
        int rc = reset_demo(state);
        if (rc == TSEDGE_OK) {
            set_last_command(state, command, "ok", "Demo сброшено.", 0);
            add_event(state, "sensor", "Demo сброшено и запущено заново.");
        } else {
            set_last_command(state, command, "error", tsedge_strerror(rc), 0);
        }
        return rc;
    } else if (strcmp(command, "pause") == 0) {
        state->paused = 1;
        set_last_command(state, command, "ok", "Пауза включена.", 0);
        add_event(state, "sensor", "Генерация точек поставлена на паузу.");
    } else if (strcmp(command, "resume") == 0) {
        state->paused = 0;
        state->crashed = 0;
        set_last_command(state, command, "ok", "Генерация продолжена.", 0);
        add_event(state, "sensor", "Генерация точек продолжена.");
    } else {
        set_last_command(state, command, "error", "Неизвестная команда.", 0);
    }
    return TSEDGE_OK;
}
