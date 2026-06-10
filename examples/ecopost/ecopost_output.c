#define _POSIX_C_SOURCE 200809L

#include "ecopost_output.h"
#include "ecopost_fs.h"
#include "ecopost_ops.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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

int write_storage_tree(agent_state* state) {
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

int write_events_log(agent_state* state) {
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

static void write_json_string(FILE* out, const char* text) {
    fputc('"', out);
    if (text) {
        for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
            switch (*p) {
                case '"':
                    fputs("\\\"", out);
                    break;
                case '\\':
                    fputs("\\\\", out);
                    break;
                case '\n':
                    fputs("\\n", out);
                    break;
                case '\r':
                    fputs("\\r", out);
                    break;
                case '\t':
                    fputs("\\t", out);
                    break;
                default:
                    if (*p < 0x20u) {
                        fputc(' ', out);
                    } else {
                        fputc((int)*p, out);
                    }
                    break;
            }
        }
    }
    fputc('"', out);
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

static void write_series_list_json(FILE* out, agent_state* state) {
    tsedge_series_info* list = NULL;
    size_t count = 0;
    int rc = tsedge_list_series(state->db, &list, &count);
    if (rc != TSEDGE_OK) {
        fputs("  \"series_list\": {\"status\":\"error\",\"message\":\"Не удалось получить список рядов\",\"count\":0,\"items\":[]},\n", out);
        return;
    }

    fprintf(out, "  \"series_list\": {\"status\":\"ok\",\"count\":%zu,\"items\":[\n", count);
    for (size_t i = 0; i < count; ++i) {
        fputs("    {\"name\":", out);
        write_json_string(out, list[i].name);
        fprintf(out,
                ",\"total_points\":%llu,\"segment_count\":%u,\"block_count\":%u,\"compressed_size_bytes\":%llu}%s\n",
                (unsigned long long)list[i].total_points,
                list[i].segment_count,
                list[i].block_count,
                (unsigned long long)list[i].compressed_size_bytes,
                i + 1u == count ? "" : ",");
    }
    fputs("  ]},\n", out);
    tsedge_free_series_list(list);
}

int write_live_state(agent_state* state) {
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

    int64_t timestamp = ecopost_simulated_timestamp(state);
    double percent = stats.buffered_points ? ((double)stats.buffered_points * 100.0 / (double)BUFFER_CAPACITY) : 0.0;
    int block_created = stats.buffered_points < 12u;
    storage_totals storage = collect_storage_totals(state);

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
    fprintf(out,
            "  \"storage\": {\"raw_size_estimate_bytes\":%llu,\"compressed_size_bytes\":%llu,\"compression_ratio\":%.6f,\"bytes_per_point\":%.6f},\n",
            (unsigned long long)storage.raw_size_estimate_bytes,
            (unsigned long long)storage.compressed_size_bytes,
            storage.compression_ratio,
            storage.bytes_per_point);
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
    fputs("  \"last_command\": {", out);
    if (state->last_command.name[0]) {
        fputs("\"name\":", out);
        write_json_string(out, state->last_command.name);
    } else {
        fputs("\"name\":null", out);
    }
    fputs(",\"status\":", out);
    write_json_string(out, state->last_command.status[0] ? state->last_command.status : "not_run");
    fputs(",\"message\":", out);
    write_json_string(out, state->last_command.message);
    fprintf(out,
            ",\"affected_points\":%zu,\"timestamp\":%lld},\n",
            state->last_command.affected_points,
            (long long)state->last_command.timestamp);

    fputs("  \"last_query\": {", out);
    if (strcmp(state->last_query.status, "not_run") == 0 || !state->last_query.type[0]) {
        fputs("\"type\":null,\"series\":null,\"status\":\"not_run\",\"from\":0,\"to\":0,\"points_read\":0,\"result\":0.0,\"min\":0.0,\"max\":0.0,\"duration_ms\":0.0},\n", out);
    } else {
        fputs("\"type\":", out);
        write_json_string(out, state->last_query.type);
        fputs(",\"series\":", out);
        write_json_string(out, state->last_query.series);
        fputs(",\"status\":", out);
        write_json_string(out, state->last_query.status);
        fprintf(out,
                ",\"from\":%lld,\"to\":%lld,\"points_read\":%zu,\"result\":%.6f,\"min\":%.6f,\"max\":%.6f,\"duration_ms\":%.3f},\n",
                (long long)state->last_query.from_timestamp,
                (long long)state->last_query.to_timestamp,
                state->last_query.points_read,
                state->last_query.result,
                state->last_query.min_value,
                state->last_query.max_value,
                state->last_query.duration_ms);
    }

    if (state->verify_last_run) {
        fprintf(out,
                "  \"verify\": {\"last_run\":true,\"available\":true,\"ok\":%s,\"series_count\":%zu,\"segment_count\":%zu,\"block_count\":%zu,\"wal_entry_count\":%zu,\"error_count\":%zu,",
                state->verify_ok ? "true" : "false",
                state->verify_report.series_count,
                state->verify_report.segment_count,
                state->verify_report.block_count,
                state->verify_report.wal_entry_count,
                state->verify_report.error_count);
        if (state->verify_report.error_count > 0) {
            fputs("\"first_error_path\":", out);
            write_json_string(out, state->verify_report.first_error_path);
            fputs(",\"first_error_message\":", out);
            write_json_string(out, state->verify_report.first_error_message);
            fputs("},\n", out);
        } else {
            fputs("\"first_error_path\":null,\"first_error_message\":null},\n", out);
        }
    } else {
        fputs("  \"verify\": {\"last_run\":false,\"available\":true,\"ok\":false,\"series_count\":0,\"segment_count\":0,\"block_count\":0,\"wal_entry_count\":0,\"error_count\":0,\"first_error_path\":null,\"first_error_message\":null},\n", out);
    }
    write_series_list_json(out, state);
    write_events_json(out, state);
    fputs("}\n", out);

    if (fclose(out) != 0) {
        return -1;
    }
    return rename(tmp_path, final_path);
}
