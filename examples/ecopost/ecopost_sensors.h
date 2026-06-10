#ifndef ECOPOST_SENSORS_H
#define ECOPOST_SENSORS_H

#include <stddef.h>
#include <stdint.h>

#define START_TIMESTAMP 1710000000000LL
#define STEP_MS 5000LL
#define SERIES_COUNT 6u
#define HISTORY_POINTS 9000u
#define BATCH_SIZE 1024u
#define DIAG_BATCH_SIZE 512u
#define BUFFER_CAPACITY 4096u

typedef struct {
    const char* name;
    const char* title;
    const char* unit;
} sensor_def;

extern const sensor_def SENSORS[SERIES_COUNT];

/* Generates a deterministic sensor value for one simulated time step. */
double sensor_value(size_t index, size_t sensor_index, int pollution_active);

#endif
