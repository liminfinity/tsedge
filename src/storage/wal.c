#define _POSIX_C_SOURCE 200809L

#include "wal.h"
#include "bitstream.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TSEDGE_WAL_MAGIC 0x57455354u
#define TSEDGE_WAL_VERSION 2u
#define TSEDGE_WAL_FIXED_SIZE 32u
#define TSEDGE_WAL_CHECKSUM_SIZE 4u
#define TSEDGE_WAL_MIN_ENTRY_SIZE (TSEDGE_WAL_FIXED_SIZE + 1u + TSEDGE_WAL_CHECKSUM_SIZE)

static int write_exact(FILE* f, const void* data, size_t size) {
    return fwrite(data, 1, size, f) == size ? TSEDGE_OK : TSEDGE_ERR_IO;
}

static uint32_t fnv1a32(const uint8_t* data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static int wal_sync(FILE* f) {
    if (fflush(f) != 0) {
        return TSEDGE_ERR_IO;
    }
#ifdef TSEDGE_WAL_FSYNC
    int fd = fileno(f);
    if (fd < 0) {
        return TSEDGE_ERR_IO;
    }
    if (fsync(fd) != 0) {
        return TSEDGE_ERR_IO;
    }
#endif
    return TSEDGE_OK;
}

static int wal_build_entry(const char* name, const tsedge_point* point, uint8_t** out_entry, size_t* out_size) {
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > TSEDGE_MAX_SERIES_NAME) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    size_t entry_size = TSEDGE_WAL_FIXED_SIZE + name_len + TSEDGE_WAL_CHECKSUM_SIZE;
    if (entry_size > UINT32_MAX) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    uint8_t* entry = (uint8_t*)malloc(entry_size);
    if (!entry) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    tsedge_write_u32_le(entry, TSEDGE_WAL_MAGIC);
    tsedge_write_u32_le(entry + 4, TSEDGE_WAL_VERSION);
    tsedge_write_u32_le(entry + 8, (uint32_t)entry_size);
    tsedge_write_u32_le(entry + 12, (uint32_t)name_len);
    tsedge_write_u64_le(entry + 16, (uint64_t)point->timestamp);

    uint64_t bits = 0;
    memcpy(&bits, &point->value, sizeof(bits));
    tsedge_write_u64_le(entry + 24, bits);
    memcpy(entry + TSEDGE_WAL_FIXED_SIZE, name, name_len);

    uint32_t checksum = fnv1a32(entry, entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
    tsedge_write_u32_le(entry + entry_size - TSEDGE_WAL_CHECKSUM_SIZE, checksum);

    *out_entry = entry;
    *out_size = entry_size;
    return TSEDGE_OK;
}

static int wal_write_entry(FILE* f, const char* name, const tsedge_point* point) {
    /*
     * WAL v2 entry format:
     *   u32 magic "TSEW"
     *   u32 version = 2
     *   u32 entry_size
     *   u32 series_name_length
     *   i64 timestamp
     *   u64 raw double bits
     *   bytes series_name
     *   u32 FNV-1a checksum over all previous entry bytes
     */
    uint8_t* entry = NULL;
    size_t entry_size = 0;
    int rc = wal_build_entry(name, point, &entry, &entry_size);
    if (rc == TSEDGE_OK) {
        rc = write_exact(f, entry, entry_size);
    }
    free(entry);
    return rc;
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
    if (rc == TSEDGE_OK) {
        rc = wal_sync(f);
    }
    if (fclose(f) != 0 && rc == TSEDGE_OK) {
        rc = TSEDGE_ERR_IO;
    }
    return rc;
}

int tsedge_wal_append_batch(tsedge_db* db, const char* series_name, const tsedge_point* points, size_t count) {
    if (!db || !series_name || (!points && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return TSEDGE_OK;
    }

    /*
     * Batch append keeps the WAL-before-buffer rule, but amortizes file open
     * and sync overhead across a buffer-sized chunk of points.
     */
    FILE* f = fopen(db->wal_path, "ab");
    if (!f) {
        return TSEDGE_ERR_IO;
    }

    int rc = TSEDGE_OK;
    for (size_t i = 0; i < count; ++i) {
        rc = wal_write_entry(f, series_name, &points[i]);
        if (rc != TSEDGE_OK) {
            break;
        }
    }
    if (rc == TSEDGE_OK) {
        rc = wal_sync(f);
    }
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

    int rc = wal_sync(f);
    if (fclose(f) != 0 && rc == TSEDGE_OK) {
        rc = TSEDGE_ERR_IO;
    }
    return rc;
}

static int replay_entry(tsedge_db* db, const uint8_t* entry, size_t entry_size) {
    uint32_t magic = tsedge_read_u32_le(entry);
    uint32_t version = tsedge_read_u32_le(entry + 4);
    uint32_t stored_entry_size = tsedge_read_u32_le(entry + 8);
    uint32_t name_len = tsedge_read_u32_le(entry + 12);

    if (magic != TSEDGE_WAL_MAGIC ||
        version != TSEDGE_WAL_VERSION ||
        stored_entry_size != entry_size ||
        entry_size < TSEDGE_WAL_MIN_ENTRY_SIZE ||
        name_len == 0 ||
        name_len > TSEDGE_MAX_SERIES_NAME ||
        entry_size != TSEDGE_WAL_FIXED_SIZE + (size_t)name_len + TSEDGE_WAL_CHECKSUM_SIZE) {
        return TSEDGE_ERR_CORRUPT;
    }

    uint32_t expected_checksum = tsedge_read_u32_le(entry + entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
    uint32_t actual_checksum = fnv1a32(entry, entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
    if (expected_checksum != actual_checksum) {
        return TSEDGE_ERR_CORRUPT;
    }

    char* name = (char*)malloc((size_t)name_len + 1u);
    if (!name) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    memcpy(name, entry + TSEDGE_WAL_FIXED_SIZE, name_len);
    name[name_len] = '\0';

    int rc = tsedge_series_validate_name(name);
    if (rc != TSEDGE_OK) {
        free(name);
        return TSEDGE_ERR_CORRUPT;
    }
    rc = tsedge_db_add_series_object(db, name, 1);
    if (rc != TSEDGE_OK) {
        free(name);
        return rc;
    }

    tsedge_point point;
    point.timestamp = (int64_t)tsedge_read_u64_le(entry + 16);
    uint64_t bits = tsedge_read_u64_le(entry + 24);
    memcpy(&point.value, &bits, sizeof(point.value));

    tsedge_series* series = tsedge_db_find_series(db, name);
    free(name);
    return tsedge_series_add_recovered_point(db, series, &point);
}

int tsedge_wal_replay(tsedge_db* db) {
    /*
     * Replay runs during open and rebuilds in-memory buffers from WAL entries.
     * A torn final entry is ignored, which models a crash during the last write.
     */
    FILE* f = fopen(db->wal_path, "rb");
    if (!f) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    for (;;) {
        uint8_t prefix[12];
        size_t n = fread(prefix, 1, sizeof(prefix), f);
        if (n == 0 && feof(f)) {
            break;
        }
        if (n != sizeof(prefix)) {
            break;
        }

        uint32_t entry_size = tsedge_read_u32_le(prefix + 8);
        if (entry_size < TSEDGE_WAL_MIN_ENTRY_SIZE || entry_size > 1024u * 1024u) {
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }

        uint8_t* entry = (uint8_t*)malloc(entry_size);
        if (!entry) {
            fclose(f);
            return TSEDGE_ERR_NO_MEMORY;
        }
        memcpy(entry, prefix, sizeof(prefix));
        size_t remaining = entry_size - sizeof(prefix);
        if (fread(entry + sizeof(prefix), 1, remaining, f) != remaining) {
            free(entry);
            break;
        }

        int rc = replay_entry(db, entry, entry_size);
        free(entry);
        if (rc != TSEDGE_OK) {
            fclose(f);
            return rc;
        }
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

int tsedge_wal_verify_file(const char* wal_path, size_t* out_entry_count) {
    if (!wal_path || !out_entry_count) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    *out_entry_count = 0;

    FILE* f = fopen(wal_path, "rb");
    if (!f) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    for (;;) {
        uint8_t prefix[12];
        size_t n = fread(prefix, 1, sizeof(prefix), f);
        if (n == 0 && feof(f)) {
            break;
        }
        if (n != sizeof(prefix)) {
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }

        uint32_t entry_size = tsedge_read_u32_le(prefix + 8);
        if (entry_size < TSEDGE_WAL_MIN_ENTRY_SIZE || entry_size > 1024u * 1024u) {
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }

        uint8_t* entry = (uint8_t*)malloc(entry_size);
        if (!entry) {
            fclose(f);
            return TSEDGE_ERR_NO_MEMORY;
        }
        memcpy(entry, prefix, sizeof(prefix));
        size_t remaining = entry_size - sizeof(prefix);
        if (fread(entry + sizeof(prefix), 1, remaining, f) != remaining) {
            free(entry);
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }

        uint32_t magic = tsedge_read_u32_le(entry);
        uint32_t version = tsedge_read_u32_le(entry + 4);
        uint32_t stored_entry_size = tsedge_read_u32_le(entry + 8);
        uint32_t name_len = tsedge_read_u32_le(entry + 12);
        uint32_t expected_checksum = tsedge_read_u32_le(entry + entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
        uint32_t actual_checksum = fnv1a32(entry, entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
        int valid = magic == TSEDGE_WAL_MAGIC &&
            version == TSEDGE_WAL_VERSION &&
            stored_entry_size == entry_size &&
            name_len > 0 &&
            name_len <= TSEDGE_MAX_SERIES_NAME &&
            entry_size == TSEDGE_WAL_FIXED_SIZE + (size_t)name_len + TSEDGE_WAL_CHECKSUM_SIZE &&
            expected_checksum == actual_checksum;
        free(entry);
        if (!valid) {
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }
        ++(*out_entry_count);
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}
