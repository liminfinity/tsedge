#include "ecopost_commands.h"
#include "ecopost_fs.h"
#include "ecopost_ops.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int read_command(agent_state* state, char* command, size_t command_size) {
    char path[1024];
    path_join(path, sizeof(path), state->config.output_path, "command.json");
    FILE* f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    unlink(path);

    const char* key = "\"command\"";
    char* p = strstr(buf, key);
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
    if (len >= command_size) {
        len = command_size - 1u;
    }
    memcpy(command, p, len);
    command[len] = '\0';
    return 1;
}

int handle_command(agent_state* state, const char* command) {
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
    } else if (strcmp(command, "append_batch") == 0) {
        return append_batch_command(state);
    } else if (strcmp(command, "flush_all") == 0) {
        return flush_all_command(state);
    } else if (strcmp(command, "read_last_range") == 0) {
        return read_last_range_command(state);
    } else if (strcmp(command, "aggregate_avg") == 0) {
        return aggregate_avg_command(state);
    } else if (strcmp(command, "aggregate_min_max") == 0) {
        return aggregate_min_max_command(state);
    } else if (strcmp(command, "run_retention") == 0) {
        return run_retention(state);
    } else if (strcmp(command, "export_csv") == 0) {
        if (!state->network_online) {
            set_last_command(state, command, "error", "CSV нельзя выгрузить без связи.", 0);
            add_event(state, "export", "CSV нельзя выгрузить без связи.");
            return TSEDGE_OK;
        }
        return export_csv(state);
    } else if (strcmp(command, "verify_db") == 0) {
        return verify_db(state);
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
