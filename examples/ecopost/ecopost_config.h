#ifndef ECOPOST_CONFIG_H
#define ECOPOST_CONFIG_H

typedef struct {
    int live;
    int interval_ms;
    int duration_sec;
    const char* db_path;
    const char* output_path;
} agent_config;

/* Prints command-line usage for the ecopost agent. */
void ecopost_print_help(void);

/* Parses agent options and fills defaults for omitted values. */
int ecopost_parse_args(int argc, char** argv, agent_config* config);

#endif
