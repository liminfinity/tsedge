#include "tsedge.h"

#include <stdio.h>
#include <unistd.h>

static const char* default_db_path = "crash_recovery_demo_db";
static const char* series_name = "crash.temperature";

static void fail_now(const char* operation, int status) {
    fprintf(stderr, "%s failed: %s\n", operation, tsedge_strerror(status));
    fflush(stderr);
    _exit(2);
}

int main(int argc, char** argv) {
    const char* db_path = argc > 1 ? argv[1] : default_db_path;
    tsedge_db* db = NULL;

    printf("Crash writer\n");
    printf("database: %s\n", db_path);

    int rc = tsedge_open(db_path, &db);
    if (rc != TSEDGE_OK) {
        fail_now("tsedge_open", rc);
    }
    rc = tsedge_create_series(db, series_name);
    if (rc != TSEDGE_OK) {
        fail_now("tsedge_create_series", rc);
    }

    for (int i = 0; i < 5; ++i) {
        rc = tsedge_append(db, series_name, (int64_t)i + 1, 11.0 + (double)i);
        if (rc != TSEDGE_OK) {
            fail_now("tsedge_append", rc);
        }
    }

    printf("written points: 5\n");
    printf("simulating crash without tsedge_close\n");
    fflush(stdout);

    _exit(1);
}
