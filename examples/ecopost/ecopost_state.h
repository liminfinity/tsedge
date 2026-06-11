#ifndef ECOPOST_STATE_H
#define ECOPOST_STATE_H

#include "ecopost_config.h"
#include "ecopost_sensors.h"
#include "tsedge.h"

#include <stddef.h>
#include <stdint.h>

#define MAX_EVENTS 40u
#define EVENT_TEXT_SIZE 160u

typedef struct {
    char time[16];
    char type[24];
    char message[EVENT_TEXT_SIZE];
} event_entry;

typedef struct {
    char name[64];
    char status[16];
    char message[EVENT_TEXT_SIZE];
    size_t affected_points;
    int64_t timestamp;
} last_command_state;

typedef struct {
    char type[24];
    char series[64];
    char status[16];
    int64_t from_timestamp;
    int64_t to_timestamp;
    size_t points_read;
    double result;
    double min_value;
    double max_value;
    double duration_ms;
} last_query_state;

typedef struct {
    uint64_t raw_size_estimate_bytes;
    uint64_t compressed_size_bytes;
    double compression_ratio;
    double bytes_per_point;
} storage_totals;

typedef struct {
    int last_run;
    int ok;
    char series[128];
    int64_t window_size;
    size_t window_count;
    size_t source_points_estimate;
    double downsample_ratio;
    double query_seconds;
    double min_value;
    double max_value;
    double avg_of_avgs;
    double weighted_avg;
    tsedge_window_aggregate first;
    tsedge_window_aggregate last;
    char message[EVENT_TEXT_SIZE];
} window_aggregate_state;

typedef struct {
    tsedge_db* db;
    tsedge_series_handle* sensor_handles[SERIES_COUNT];
    tsedge_series_handle* debug_handle;
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
    int export_last_run;
    int export_ok;
    char export_series[128];
    char export_path[256];
    size_t export_rows;
    char export_message[EVENT_TEXT_SIZE];
    int retention_last_run;
    size_t retention_deleted_count;
    char retention_last_deleted[128];
    int verify_last_run;
    int verify_ok;
    tsedge_verify_report verify_report;
    window_aggregate_state window_aggregate;
    last_command_state last_command;
    last_query_state last_query;
    double last_values[SERIES_COUNT];
    event_entry events[MAX_EVENTS];
    size_t event_count;
    size_t event_start;
} agent_state;

/* Adds a short event to the live state's ring buffer. */
void add_event(agent_state* state, const char* type, const char* message);

/* Converts the current tick into the simulated timestamp in milliseconds. */
int64_t ecopost_simulated_timestamp(const agent_state* state);

/* Stores the result of the most recent command for the UI. */
void set_last_command(agent_state* state, const char* name, const char* status, const char* message, size_t affected_points);

/* Resets the last query section to the not-run state. */
void clear_last_query(agent_state* state);

/* Initializes common fields for the last diagnostic query result. */
void set_last_query_base(agent_state* state, const char* type, const char* series, int64_t from, int64_t to);

/* Returns a monotonic millisecond timestamp for measuring query duration. */
double monotonic_ms(void);

#endif
