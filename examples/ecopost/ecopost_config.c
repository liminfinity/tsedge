#include "ecopost_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_DB_PATH "ecopost_live_db"
#define DEFAULT_OUTPUT_PATH "ecopost_live_output"

void ecopost_print_help(void) {
    printf("Usage:\n");
    printf("  tsedge_ecopost_agent --live [--interval-ms N] [--duration-sec N] [--db path] [--output path]\n");
    printf("  tsedge_ecopost_agent --help\n");
}

int ecopost_parse_args(int argc, char** argv, agent_config* config) {
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
            ecopost_print_help();
            exit(0);
        } else {
            return -1;
        }
    }

    return config->live ? 0 : -1;
}
