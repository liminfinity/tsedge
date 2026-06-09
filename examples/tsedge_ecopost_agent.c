#define _POSIX_C_SOURCE 200809L

#include "tsedge.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEFAULT_DB_PATH "ecopost_live_db"
#define DEFAULT_OUTPUT_PATH "ecopost_live_output"
#define START_TIMESTAMP 1710000000000LL
#define STEP_MS 5000LL
#define SERIES_COUNT 6u
#define HISTORY_POINTS 9000u
#define BATCH_SIZE 1024u
#define BUFFER_CAPACITY 4096u
#define MAX_EVENTS 40u
#define EVENT_TEXT_SIZE 160u

typedef struct {
    const char* name;
    const char* title;
    const char* unit;
} sensor_def;

typedef struct {
    char time[16];
    char type[24];
    char message[EVENT_TEXT_SIZE];
} event_entry;

typedef struct {
    int live;
    int interval_ms;
    int duration_sec;
    const char* db_path;
    const char* output_path;
} agent_config;

typedef struct {
    tsedge_db* db;
    agent_config config;
    size_t tick;
    int running;
    int paused;
    int crashed;
    int network_online;
    int pollution_ticks_left;
    int crash_simulated;
    int wal_replayed;
    size_t recovered_points;
    int csv_ready;
    char last_csv_file[128];
    int retention_last_run;
    size_t retention_deleted_count;
    char retention_last_deleted[128];
    double last_values[SERIES_COUNT];
    event_entry events[MAX_EVENTS];
    size_t event_count;
    size_t event_start;
} agent_state;

static const sensor_def SENSORS[SERIES_COUNT] = {
    {"air.temperature", "Температура", "°C"},
    {"air.humidity", "Влажность", "%"},
    {"air.pressure", "Давление", "гПа"},
    {"wind.speed", "Ветер", "м/с"},
    {"pm25.concentration", "PM2.5", "мкг/м³"},
    {"battery.voltage", "Аккумулятор", "В"},
};

static void print_help(void) {
    printf("Usage:\n");
    printf("  tsedge_ecopost_agent --live [--interval-ms N] [--duration-sec N] [--db path] [--output path]\n");
    printf("  tsedge_ecopost_agent --help\n");
}

static int parse_args(int argc, char** argv, agent_config* config) {
    config->live = 0;
    config->interval_ms = 1000;
    config->duration_sec = 0;
    config->db_path = DEFAULT_DB_PATH;
    config->output_path = DEFAULT_OUTPUT_PATH;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--live") == 0) {
            config->live = 1;
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            config->interval_ms = atoi(argv[++i]);
            if (config->interval_ms <= 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--duration-sec") == 0 && i + 1 < argc) {
            config->duration_sec = atoi(argv[++i]);
            if (config->duration_sec <= 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            config->db_path = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            config->output_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_help();
            exit(0);
        } else {
            return -1;
        }
    }

    return config->live ? 0 : -1;
}

static int make_dir(const char* path) {
    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

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

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void path_join(char* out, size_t out_size, const char* a, const char* b) {
    snprintf(out, out_size, "%s/%s", a, b);
}

static void event_time(char* out, size_t out_size) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(out, out_size, "%02d:%02d:%02d", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

static void add_event(agent_state* state, const char* type, const char* message) {
    size_t slot = 0;
    if (state->event_count < MAX_EVENTS) {
        slot = state->event_count++;
    } else {
        slot = state->event_start;
        state->event_start = (state->event_start + 1u) % MAX_EVENTS;
    }
    event_entry* event = &state->events[slot];
    event_time(event->time, sizeof(event->time));
    snprintf(event->type, sizeof(event->type), "%s", type);
    snprintf(event->message, sizeof(event->message), "%s", message);
}

static double smooth_noise(size_t i, double scale) {
    double a = sin((double)i * 0.073) * 0.5 + cos((double)i * 0.017) * 0.5;
    return a * scale;
}

static double sensor_value(size_t index, size_t sensor_index, int pollution_active) {
    double seconds = (double)((index * (size_t)STEP_MS) / 1000u);
    double day = fmod(seconds, 86400.0) / 86400.0;
    double daylight = sin((day - 0.25) * 2.0 * M_PI);
    if (daylight < 0.0) {
        daylight = 0.0;
    }
    double temperature = 12.0 + 13.0 * sin((day - 0.32) * 2.0 * M_PI) + smooth_noise(index, 0.45);

    switch (sensor_index) {
        case 0:
            return temperature;
        case 1:
            return 74.0 - (temperature - 12.0) * 1.2 + smooth_noise(index + 31u, 1.0);
        case 2:
            return 1013.0 + 3.5 * sin(day * 2.0 * M_PI + 0.6) + smooth_noise(index, 0.18);
        case 3: {
            double gust = ((index % 1800u) > 760u && (index % 1800u) < 805u) ? 5.0 : 0.0;
            return 2.2 + daylight * 2.1 + gust + fabs(smooth_noise(index + 9u, 0.8));
        }
        case 4: {
            double manual_spike = pollution_active ? 90.0 : 0.0;
            return 18.0 + 4.0 * sin(day * 2.0 * M_PI + 1.2) + manual_spike + smooth_noise(index, 1.5);
        }
        case 5:
            return 3.62 + daylight * 0.35 - (1.0 - daylight) * 0.08 + smooth_noise(index, 0.015);
        default:
            return 0.0;
    }
}

static int create_series(agent_state* state) {
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

static int append_history(agent_state* state) {
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

static int append_live_points(agent_state* state) {
    int64_t timestamp = START_TIMESTAMP + (int64_t)state->tick * STEP_MS;
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
    add_event(state, "append", "Получено 6 новых точек от датчиков.");
    add_event(state, "wal", "Точки записаны в WAL.");
    add_event(state, "buffer", "Точки добавлены в буфер.");
    return TSEDGE_OK;
}

static int get_primary_stats(agent_state* state, tsedge_series_stats* stats) {
    return tsedge_get_series_stats(state->db, "air.temperature", stats);
}

static size_t total_segments(agent_state* state) {
    size_t total = 0;
    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        tsedge_series_stats stats;
        if (tsedge_get_series_stats(state->db, SENSORS[i].name, &stats) == TSEDGE_OK) {
            total += stats.segment_count;
        }
    }
    return total;
}

static size_t total_segment_bytes(agent_state* state) {
    size_t total = 0;
    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        tsedge_series_stats stats;
        if (tsedge_get_series_stats(state->db, SENSORS[i].name, &stats) == TSEDGE_OK) {
            total += (size_t)stats.total_segment_size_bytes;
        }
    }
    return total;
}

static int write_storage_tree_dir(FILE* out, const char* path, unsigned indent) {
    DIR* dir = opendir(path);
    if (!dir) {
        return -1;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        for (unsigned i = 0; i < indent; ++i) {
            fputs("  ", out);
        }
        fprintf(out, "%s\n", entry->d_name);

        char child[1024];
        int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(child)) {
            closedir(dir);
            return -1;
        }
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (write_storage_tree_dir(out, child, indent + 1u) != 0) {
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);
    return 0;
}

static int write_storage_tree(agent_state* state) {
    char path[1024];
    path_join(path, sizeof(path), state->config.output_path, "storage_tree.txt");
    FILE* out = fopen(path, "wb");
    if (!out) {
        return -1;
    }
    fprintf(out, "%s\n", state->config.db_path);
    int rc = write_storage_tree_dir(out, state->config.db_path, 1u);
    if (fclose(out) != 0) {
        return -1;
    }
    return rc;
}

static int write_events_log(agent_state* state) {
    char path[1024];
    path_join(path, sizeof(path), state->config.output_path, "events.log");
    FILE* out = fopen(path, "wb");
    if (!out) {
        return -1;
    }
    for (size_t i = 0; i < state->event_count; ++i) {
        size_t index = (state->event_start + i) % MAX_EVENTS;
        const event_entry* event = &state->events[index];
        fprintf(out, "[%s] %s: %s\n", event->time, event->type, event->message);
    }
    return fclose(out) == 0 ? 0 : -1;
}

static void write_events_json(FILE* out, const agent_state* state) {
    fputs("  \"events\": [\n", out);
    for (size_t i = 0; i < state->event_count; ++i) {
        size_t index = (state->event_start + i) % MAX_EVENTS;
        const event_entry* event = &state->events[index];
        fprintf(out,
                "    {\"time\":\"%s\",\"type\":\"%s\",\"message\":\"%s\"}%s\n",
                event->time,
                event->type,
                event->message,
                i + 1u == state->event_count ? "" : ",");
    }
    fputs("  ]\n", out);
}

static void write_segments_json(FILE* out, const agent_state* state, const tsedge_series_stats* stats) {
    char series_dir[1024];
    snprintf(series_dir, sizeof(series_dir), "%s/series/air.temperature", state->config.db_path);
    DIR* dir = opendir(series_dir);
    fputs("  \"segments\": [\n", out);
    size_t written = 0;
    if (dir) {
        struct dirent* entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            unsigned id = 0;
            if (sscanf(entry->d_name, "segment_%06u.tse", &id) != 1) {
                continue;
            }
            if (written > 0) {
                fputs(",\n", out);
            }
            const char* status = id == stats->active_segment_id ? "active" : "sealed";
            int old = id + 1u < stats->active_segment_id;
            size_t blocks = stats->segment_count ? (stats->block_count / stats->segment_count) : 0u;
            fprintf(out,
                    "    {\"name\":\"%s\",\"status\":\"%s\",\"blocks\":%zu,\"old\":%s,\"deleted\":false}",
                    entry->d_name,
                    status,
                    blocks ? blocks : 1u,
                    old ? "true" : "false");
            ++written;
        }
        closedir(dir);
    }
    fputs("\n  ],\n", out);
}

static int write_live_state(agent_state* state) {
    tsedge_series_stats stats;
    memset(&stats, 0, sizeof(stats));
    if (get_primary_stats(state, &stats) != TSEDGE_OK) {
        stats.active_segment_id = 1;
    }

    char tmp_path[1024];
    char final_path[1024];
    path_join(tmp_path, sizeof(tmp_path), state->config.output_path, "live_state.json.tmp");
    path_join(final_path, sizeof(final_path), state->config.output_path, "live_state.json");

    FILE* out = fopen(tmp_path, "wb");
    if (!out) {
        return -1;
    }

    int64_t timestamp = START_TIMESTAMP + (int64_t)state->tick * STEP_MS;
    double percent = stats.buffered_points ? ((double)stats.buffered_points * 100.0 / (double)BUFFER_CAPACITY) : 0.0;
    int block_created = stats.buffered_points < 12u;

    fprintf(out, "{\n");
    fprintf(out, "  \"running\": %s,\n", state->running ? "true" : "false");
    fprintf(out, "  \"tick\": %zu,\n", state->tick);
    fprintf(out, "  \"mode\": \"live\",\n");
    fprintf(out, "  \"scenario\": {\"title\":\"Автономный экологический пост\",\"short_description\":\"Пост пишет данные локально, потому что связь может пропадать.\"},\n");
    fprintf(out, "  \"network\": {\"online\": %s,\"status\":\"%s\"},\n",
            state->network_online ? "true" : "false",
            state->network_online ? "online" : "offline");
    fprintf(out, "  \"agent\": {\"status\":\"%s\",\"last_update_ms\":%lld,\"points_written_per_series\":%zu,\"total_points_written\":%zu},\n",
            state->crashed ? "crashed" : (state->paused ? "paused" : (state->running ? "running" : "stopped")),
            (long long)timestamp,
            state->tick,
            state->tick * SERIES_COUNT);
    fprintf(out, "  \"last_batch\": {\"point_count\":6,\"timestamp\":%lld,\"series\":[\n", (long long)timestamp);
    for (size_t i = 0; i < SERIES_COUNT; ++i) {
        fprintf(out,
                "    {\"name\":\"%s\",\"title\":\"%s\",\"value\":%.6f,\"unit\":\"%s\"}%s\n",
                SENSORS[i].name,
                SENSORS[i].title,
                state->last_values[i],
                SENSORS[i].unit,
                i + 1u == SERIES_COUNT ? "" : ",");
    }
    fputs("  ]},\n", out);
    fprintf(out, "  \"wal\": {\"status\":\"protected\",\"last_write\":true,\"entries_since_flush\":%zu,\"replayed\":%s},\n",
            stats.buffered_points * SERIES_COUNT,
            state->wal_replayed ? "true" : "false");
    fprintf(out, "  \"buffer\": {\"points\":%zu,\"capacity\":%u,\"percent\":%.2f},\n",
            stats.buffered_points,
            BUFFER_CAPACITY,
            percent);
    fprintf(out, "  \"compression\": {\"active\":true,\"last_block_points\":%u,\"last_block_created\":%s,\"algorithm\":\"delta-of-delta + XOR\"},\n",
            BUFFER_CAPACITY,
            block_created ? "true" : "false");
    write_segments_json(out, state, &stats);
    if (state->retention_last_deleted[0]) {
        fprintf(out,
                "  \"retention\": {\"enabled\":true,\"last_run\":%s,\"last_deleted\":\"%s\",\"deleted_count\":%zu},\n",
                state->retention_last_run ? "true" : "false",
                state->retention_last_deleted,
                state->retention_deleted_count);
    } else {
        fprintf(out,
                "  \"retention\": {\"enabled\":true,\"last_run\":%s,\"last_deleted\":null,\"deleted_count\":%zu},\n",
                state->retention_last_run ? "true" : "false",
                state->retention_deleted_count);
    }
    fprintf(out, "  \"recovery\": {\"crash_simulated\":%s,\"wal_replayed\":%s,\"recovered_points\":%zu},\n",
            state->crash_simulated ? "true" : "false",
            state->wal_replayed ? "true" : "false",
            state->recovered_points);
    if (state->last_csv_file[0]) {
        fprintf(out,
                "  \"export\": {\"csv_ready\":%s,\"last_file\":\"%s\"},\n",
                state->csv_ready ? "true" : "false",
                state->last_csv_file);
    } else {
        fprintf(out,
                "  \"export\": {\"csv_ready\":%s,\"last_file\":null},\n",
                state->csv_ready ? "true" : "false");
    }
    write_events_json(out, state);
    fputs("}\n", out);

    if (fclose(out) != 0) {
        return -1;
    }
    return rename(tmp_path, final_path);
}

static int read_command(agent_state* state, char* command, size_t command_size) {
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

static int reopen_db(agent_state* state) {
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

static int run_retention(agent_state* state) {
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
    add_event(state, "retention", state->retention_deleted_count ? "Retention удалил старые segment-файлы." : "Retention запущен, но полных старых segment-файлов нет.");
    return TSEDGE_OK;
}

static int export_csv(agent_state* state) {
    char path[1024];
    path_join(path, sizeof(path), state->config.output_path, "ecopost_temperature.csv");
    int64_t to = START_TIMESTAMP + (int64_t)state->tick * STEP_MS;
    int rc = tsedge_export_csv(state->db, "air.temperature", START_TIMESTAMP, to, path);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    state->csv_ready = 1;
    snprintf(state->last_csv_file, sizeof(state->last_csv_file), "ecopost_temperature.csv");
    add_event(state, "export", "CSV-файл готов для выгрузки.");
    return TSEDGE_OK;
}

static int reset_demo(agent_state* state) {
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

static int handle_command(agent_state* state, const char* command) {
    if (strcmp(command, "network_offline") == 0) {
        state->network_online = 0;
        add_event(state, "network", "Связь потеряна. Запись продолжается локально.");
    } else if (strcmp(command, "network_online") == 0) {
        state->network_online = 1;
        add_event(state, "network", "Связь восстановлена. Данные можно выгрузить.");
    } else if (strcmp(command, "simulate_pollution") == 0) {
        state->pollution_ticks_left = 90;
        add_event(state, "pollution", "Зафиксирован всплеск PM2.5.");
    } else if (strcmp(command, "simulate_crash") == 0) {
        state->crashed = 1;
        state->paused = 1;
        state->crash_simulated = 1;
        add_event(state, "recovery", "Сбой питания. Запись временно остановлена.");
    } else if (strcmp(command, "recover_from_wal") == 0) {
        int rc = reopen_db(state);
        if (rc != TSEDGE_OK) {
            return rc;
        }
        state->crashed = 0;
        state->paused = 0;
        state->wal_replayed = 1;
        state->recovered_points = 6u;
        add_event(state, "recovery", "Данные восстановлены из WAL.");
    } else if (strcmp(command, "run_retention") == 0) {
        return run_retention(state);
    } else if (strcmp(command, "export_csv") == 0) {
        if (!state->network_online) {
            add_event(state, "export", "CSV нельзя выгрузить без связи.");
            return TSEDGE_OK;
        }
        return export_csv(state);
    } else if (strcmp(command, "reset_demo") == 0) {
        int rc = reset_demo(state);
        if (rc == TSEDGE_OK) {
            add_event(state, "sensor", "Demo сброшено и запущено заново.");
        }
        return rc;
    } else if (strcmp(command, "pause") == 0) {
        state->paused = 1;
        add_event(state, "sensor", "Генерация точек поставлена на паузу.");
    } else if (strcmp(command, "resume") == 0) {
        state->paused = 0;
        state->crashed = 0;
        add_event(state, "sensor", "Генерация точек продолжена.");
    }
    return TSEDGE_OK;
}

static int run_agent(agent_state* state) {
    int rc = reset_demo(state);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "init failed: %s\n", tsedge_strerror(rc));
        return rc;
    }

    state->running = 1;
    add_event(state, "wal", "WAL защищает точки перед буфером.");

    printf("TSEdge ecopost live simulation\n");
    printf("database: %s\n", state->config.db_path);
    printf("output: %s\n", state->config.output_path);
    printf("created series: %u\n", SERIES_COUNT);
    printf("interval: %d ms\n", state->config.interval_ms);
    printf("commands: %s/command.json\n", state->config.output_path);

    int elapsed_ms = 0;
    for (;;) {
        char command[64];
        if (read_command(state, command, sizeof(command))) {
            rc = handle_command(state, command);
            if (rc != TSEDGE_OK) {
                add_event(state, "error", tsedge_strerror(rc));
            }
        }

        if (!state->paused && !state->crashed) {
            rc = append_live_points(state);
            if (rc != TSEDGE_OK) {
                add_event(state, "error", tsedge_strerror(rc));
            }
        }

        write_storage_tree(state);
        write_events_log(state);
        write_live_state(state);

        if (state->config.duration_sec > 0 && elapsed_ms >= state->config.duration_sec * 1000) {
            break;
        }
        sleep_ms(state->config.interval_ms);
        elapsed_ms += state->config.interval_ms;
    }

    if (state->config.duration_sec > 0) {
        rc = reopen_db(state);
        if (rc != TSEDGE_OK) {
            add_event(state, "error", tsedge_strerror(rc));
        }
    }
    state->running = 0;
    write_storage_tree(state);
    write_events_log(state);
    write_live_state(state);
    return TSEDGE_OK;
}

int main(int argc, char** argv) {
    agent_state state;
    memset(&state, 0, sizeof(state));
    if (parse_args(argc, argv, &state.config) != 0) {
        print_help();
        return 1;
    }

    int rc = run_agent(&state);
    if (state.db) {
        int close_rc = tsedge_close(state.db);
        if (rc == TSEDGE_OK) {
            rc = close_rc;
        }
    }
    return rc == TSEDGE_OK ? 0 : 1;
}
