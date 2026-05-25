#define _POSIX_C_SOURCE 200809L

#include "csv.h"

#include <inttypes.h>
#include <stdio.h>

typedef struct {
    FILE* file;
    int status;
} csv_ctx;

static int csv_cb(const tsedge_point* point, void* user_data) {
    csv_ctx* ctx = (csv_ctx*)user_data;
    if (fprintf(ctx->file, "%" PRId64 ",%.17g\n", point->timestamp, point->value) < 0) {
        ctx->status = TSEDGE_ERR_IO;
        return 1;
    }
    return 0;
}

int tsedge_csv_export(tsedge_db* db, const char* series_name, int64_t from_timestamp, int64_t to_timestamp, const char* output_path) {
    if (!output_path) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /*
     * Export deliberately uses the same range-read path as applications, so CSV
     * output reflects what users can read from the embedded database.
     */
    FILE* file = fopen(output_path, "w");
    if (!file) {
        return TSEDGE_ERR_IO;
    }
    if (fprintf(file, "timestamp,value\n") < 0) {
        fclose(file);
        return TSEDGE_ERR_IO;
    }

    csv_ctx ctx;
    ctx.file = file;
    ctx.status = TSEDGE_OK;
    int rc = tsedge_read_range(db, series_name, from_timestamp, to_timestamp, csv_cb, &ctx);
    if (rc == TSEDGE_OK) {
        rc = ctx.status;
    }
    if (fclose(file) != 0 && rc == TSEDGE_OK) {
        rc = TSEDGE_ERR_IO;
    }
    return rc;
}
