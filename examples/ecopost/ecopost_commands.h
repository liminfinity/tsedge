#ifndef ECOPOST_COMMANDS_H
#define ECOPOST_COMMANDS_H

#include "ecopost_state.h"

#include <stddef.h>

typedef struct {
    char command[64];
    char series[128];
    int64_t window_size;
    int window_size_set;
    int64_t points;
    int points_set;
} agent_command;

/* Reads one pending command from command.json, if present. */
int read_command(agent_state* state, agent_command* command);

/* Applies a command from the web simulator to the live agent state. */
int handle_command(agent_state* state, const agent_command* command);

#endif
