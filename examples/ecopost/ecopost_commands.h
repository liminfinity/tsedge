#ifndef ECOPOST_COMMANDS_H
#define ECOPOST_COMMANDS_H

#include "ecopost_state.h"

#include <stddef.h>

/* Reads one pending command from command.json, if present. */
int read_command(agent_state* state, char* command, size_t command_size);

/* Applies a command from the web simulator to the live agent state. */
int handle_command(agent_state* state, const char* command);

#endif
