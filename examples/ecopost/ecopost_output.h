#ifndef ECOPOST_OUTPUT_H
#define ECOPOST_OUTPUT_H

#include "ecopost_state.h"

/* Writes a readable snapshot of the database files on disk. */
int write_storage_tree(agent_state* state);

/* Writes the recent event ring buffer as plain text. */
int write_events_log(agent_state* state);

/* Writes the current live state JSON atomically for the web UI. */
int write_live_state(agent_state* state);

#endif
