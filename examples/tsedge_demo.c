#include "tsedge.h"

#include <stdio.h>

int main(void) {
    tsedge_db* db = NULL;
    int rc = tsedge_open("./demo_db", &db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "open failed: %s\n", tsedge_strerror(rc));
        return 1;
    }

    rc = tsedge_create_series(db, "motor.temperature");
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "create_series failed: %s\n", tsedge_strerror(rc));
        tsedge_close(db);
        return 1;
    }

    for (int i = 0; i < 10000; ++i) {
        rc = tsedge_append(db, "motor.temperature", 1710000000000LL + (int64_t)i * 1000, 70.0 + i * 0.001);
        if (rc != TSEDGE_OK) {
            fprintf(stderr, "append failed: %s\n", tsedge_strerror(rc));
            tsedge_close(db);
            return 1;
        }
    }

    double avg = 0.0;
    rc = tsedge_aggregate(
        db,
        "motor.temperature",
        1710000000000LL,
        1710000100000LL,
        TSEDGE_AGG_AVG,
        &avg
    );
    if (rc == TSEDGE_OK) {
        printf("avg=%.6f\n", avg);
    }

    rc = tsedge_export_csv(
        db,
        "motor.temperature",
        1710000000000LL,
        1710000100000LL,
        "temperature.csv"
    );
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "export failed: %s\n", tsedge_strerror(rc));
    }

    rc = tsedge_close(db);
    if (rc != TSEDGE_OK) {
        fprintf(stderr, "close failed: %s\n", tsedge_strerror(rc));
        return 1;
    }
    return 0;
}
