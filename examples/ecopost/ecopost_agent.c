#include "ecopost_commands.h"
#include "ecopost_config.h"
#include "ecopost_fs.h"
#include "ecopost_ops.h"
#include "ecopost_output.h"
#include "ecopost_state.h"
#include "tsedge.h"

#include <stdio.h>
#include <string.h>

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
        agent_command command;
        if (read_command(state, &command)) {
            rc = handle_command(state, &command);
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
    if (ecopost_parse_args(argc, argv, &state.config) != 0) {
        ecopost_print_help();
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
