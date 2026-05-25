#define _POSIX_C_SOURCE 200809L

#include "wal.h"
#include "bitstream.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_exact(FILE* f, const void* data, size_t size) {
    return fwrite(data, 1, size, f) == size ? TSEDGE_OK : TSEDGE_ERR_IO;
}

static int wal_write_entry(FILE* f, const char* name, const tsedge_point* point) {
    /*
     * WAL entry format:
     *   u32 series_name_length
     *   i64 timestamp
     *   u64 raw double bits
     *   bytes series_name
     */
    size_t len = strlen(name);
    if (len == 0 || len > UINT32_MAX) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    uint8_t buf[20];
    tsedge_write_u32_le(buf, (uint32_t)len);
    tsedge_write_u64_le(buf + 4, (uint64_t)point->timestamp);
    uint64_t bits = 0;
    memcpy(&bits, &point->value, sizeof(bits));
    tsedge_write_u64_le(buf + 12, bits);

    int rc = write_exact(f, buf, sizeof(buf));
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return write_exact(f, name, len);
}

int tsedge_wal_append(tsedge_db* db, const char* series_name, const tsedge_point* point) {
    /*
     * The append path calls this before mutating the in-memory buffer, so an
     * acknowledged point can be reconstructed even if the process exits early.
     */
    FILE* f = fopen(db->wal_path, "ab");
    if (!f) {
        return TSEDGE_ERR_IO;
    }
    int rc = wal_write_entry(f, series_name, point);
    if (fclose(f) != 0 && rc == TSEDGE_OK) {
        rc = TSEDGE_ERR_IO;
    }
    return rc;
}

int tsedge_wal_truncate_to_buffers(tsedge_db* db) {
    /*
     * Once a buffer is flushed into a segment block, those points are durable in
     * the segment. The WAL can then be reduced to only the remaining buffers.
     */
    FILE* f = fopen(db->wal_path, "wb");
    if (!f) {
        return TSEDGE_ERR_IO;
    }

    for (size_t i = 0; i < db->series_count; ++i) {
        tsedge_series* series = &db->series[i];
        for (size_t j = 0; j < series->buffer_count; ++j) {
            int rc = wal_write_entry(f, series->name, &series->buffer[j]);
            if (rc != TSEDGE_OK) {
                fclose(f);
                return rc;
            }
        }
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

int tsedge_wal_replay(tsedge_db* db) {
    /*
     * Replay runs during open and rebuilds in-memory buffers from WAL entries.
     * If a series directory is missing, it is recreated before points are added.
     */
    FILE* f = fopen(db->wal_path, "rb");
    if (!f) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    for (;;) {
        uint8_t fixed[20];
        size_t n = fread(fixed, 1, sizeof(fixed), f);
        if (n == 0 && feof(f)) {
            break;
        }
        if (n != sizeof(fixed)) {
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }

        uint32_t len = tsedge_read_u32_le(fixed);
        if (len == 0 || len > TSEDGE_MAX_SERIES_NAME) {
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }

        char* name = (char*)malloc((size_t)len + 1u);
        if (!name) {
            fclose(f);
            return TSEDGE_ERR_NO_MEMORY;
        }
        if (fread(name, 1, len, f) != len) {
            free(name);
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }
        name[len] = '\0';

        int rc = tsedge_series_validate_name(name);
        if (rc != TSEDGE_OK) {
            free(name);
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }
        rc = tsedge_db_add_series_object(db, name, 1);
        if (rc != TSEDGE_OK) {
            free(name);
            fclose(f);
            return rc;
        }

        tsedge_point point;
        point.timestamp = (int64_t)tsedge_read_u64_le(fixed + 4);
        uint64_t bits = tsedge_read_u64_le(fixed + 12);
        memcpy(&point.value, &bits, sizeof(point.value));

        tsedge_series* series = tsedge_db_find_series(db, name);
        free(name);
        rc = tsedge_series_add_recovered_point(db, series, &point);
        if (rc != TSEDGE_OK) {
            fclose(f);
            return rc;
        }
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}
