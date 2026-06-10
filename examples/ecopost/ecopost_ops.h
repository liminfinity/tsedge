#ifndef ECOPOST_OPS_H
#define ECOPOST_OPS_H

#include "ecopost_state.h"

#include "tsedge.h"

/* Creates all sensor series used by the ecopost scenario. */
int create_series(agent_state* state);

/* Preloads historical points so rotation and retention are visible. */
int append_history(agent_state* state);

/* Appends one live sample for every sensor series. */
int append_live_points(agent_state* state);

/* Reads stats for the main temperature series. */
int get_primary_stats(agent_state* state, tsedge_series_stats* stats);

/* Counts segment files across all series. */
size_t total_segments(agent_state* state);

/* Aggregates compression and size statistics across all series. */
storage_totals collect_storage_totals(agent_state* state);

/* Reopens the database to demonstrate recovery through the public API. */
int reopen_db(agent_state* state);

/* Applies the demo retention policy to selected series. */
int run_retention(agent_state* state);

/* Flushes buffers and exports a CSV file for download. */
int export_csv(agent_state* state);

/* Appends several live steps for diagnostic write commands. */
int append_steps(agent_state* state, size_t steps, const char* command_name, const char* message);

/* Runs a public batch append command for all series. */
int append_batch_command(agent_state* state);

/* Flushes all series buffers through the public flush API. */
int flush_all_command(agent_state* state);

/* Reads the latest simulated time window and records the result. */
int read_last_range_command(agent_state* state);

/* Computes the latest average temperature diagnostic query. */
int aggregate_avg_command(agent_state* state);

/* Computes the latest min/max diagnostic query. */
int aggregate_min_max_command(agent_state* state);

/* Runs database integrity verification and stores the report. */
int verify_db(agent_state* state);

/* Recreates the demo database and resets live state. */
int reset_demo(agent_state* state);

#endif
