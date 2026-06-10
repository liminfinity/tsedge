#define _POSIX_C_SOURCE 200809L

#include "ecopost_state.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void event_time(char* out, size_t out_size) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(out, out_size, "%02d:%02d:%02d", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

void add_event(agent_state* state, const char* type, const char* message) {
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

int64_t ecopost_simulated_timestamp(const agent_state* state) {
    return START_TIMESTAMP + (int64_t)state->tick * STEP_MS;
}

void set_last_command(agent_state* state, const char* name, const char* status, const char* message, size_t affected_points) {
    snprintf(state->last_command.name, sizeof(state->last_command.name), "%s", name ? name : "");
    snprintf(state->last_command.status, sizeof(state->last_command.status), "%s", status ? status : "ok");
    snprintf(state->last_command.message, sizeof(state->last_command.message), "%s", message ? message : "");
    state->last_command.affected_points = affected_points;
    state->last_command.timestamp = ecopost_simulated_timestamp(state);
}

void clear_last_query(agent_state* state) {
    memset(&state->last_query, 0, sizeof(state->last_query));
    snprintf(state->last_query.status, sizeof(state->last_query.status), "not_run");
}

void set_last_query_base(agent_state* state, const char* type, const char* series, int64_t from, int64_t to) {
    snprintf(state->last_query.type, sizeof(state->last_query.type), "%s", type ? type : "");
    snprintf(state->last_query.series, sizeof(state->last_query.series), "%s", series ? series : "");
    snprintf(state->last_query.status, sizeof(state->last_query.status), "ok");
    state->last_query.from_timestamp = from;
    state->last_query.to_timestamp = to;
}

double monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}
