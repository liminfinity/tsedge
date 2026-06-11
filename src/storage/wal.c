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
#define TSEDGE_WAL_VERSION_V2 2u
#define TSEDGE_WAL_VERSION_V3 3u
#define TSEDGE_WAL_V2_POINT_FIXED_SIZE 32u
#define TSEDGE_WAL_V3_POINT_FIXED_SIZE 36u
#define TSEDGE_WAL_V3_BATCH_FIXED_SIZE 24u
#define TSEDGE_WAL_POINT_PAYLOAD_SIZE 16u
#define TSEDGE_WAL_CHECKSUM_SIZE 4u
#define TSEDGE_WAL_V2_MIN_ENTRY_SIZE (TSEDGE_WAL_V2_POINT_FIXED_SIZE + 1u + TSEDGE_WAL_CHECKSUM_SIZE)
#define TSEDGE_WAL_V3_POINT_MIN_ENTRY_SIZE (TSEDGE_WAL_V3_POINT_FIXED_SIZE + 1u + TSEDGE_WAL_CHECKSUM_SIZE)
#define TSEDGE_WAL_V3_BATCH_MIN_ENTRY_SIZE (TSEDGE_WAL_V3_BATCH_FIXED_SIZE + 1u + TSEDGE_WAL_POINT_PAYLOAD_SIZE + TSEDGE_WAL_CHECKSUM_SIZE)
#ifndef TSEDGE_WAL_BUFFER_BYTES
#define TSEDGE_WAL_BUFFER_BYTES (256u * 1024u)
#endif
#ifndef TSEDGE_WAL_MAX_BATCH_POINTS
#define TSEDGE_WAL_MAX_BATCH_POINTS 65536u
#endif
#define TSEDGE_WAL_BALANCED_FLUSH_BYTES (64u * 1024u)
#define TSEDGE_WAL_MAX_POINT_ENTRY_SIZE (TSEDGE_WAL_V3_POINT_FIXED_SIZE + TSEDGE_MAX_SERIES_NAME + TSEDGE_WAL_CHECKSUM_SIZE)
#define TSEDGE_WAL_MAX_ENTRY_SIZE (TSEDGE_WAL_V3_BATCH_FIXED_SIZE + TSEDGE_MAX_SERIES_NAME + TSEDGE_WAL_CHECKSUM_SIZE + ((size_t)TSEDGE_WAL_MAX_BATCH_POINTS * TSEDGE_WAL_POINT_PAYLOAD_SIZE))

typedef enum {
    TSEDGE_WAL_RECORD_POINT = 1,
    TSEDGE_WAL_RECORD_BATCH = 2
} tsedge_wal_record_type;

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

static size_t wal_flush_threshold(const tsedge_db* db) {
    if (db->durability == TSEDGE_DURABILITY_BALANCED) {
        return TSEDGE_WAL_BALANCED_FLUSH_BYTES;
    }
    return TSEDGE_WAL_BUFFER_BYTES;
}

static int wal_v3_batch_entry_size(size_t name_len, size_t count, size_t* out_size) {
    if (!out_size || name_len == 0 || name_len > TSEDGE_MAX_SERIES_NAME || count == 0 || count > TSEDGE_WAL_MAX_BATCH_POINTS) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count > (SIZE_MAX - TSEDGE_WAL_V3_BATCH_FIXED_SIZE - name_len - TSEDGE_WAL_CHECKSUM_SIZE) / TSEDGE_WAL_POINT_PAYLOAD_SIZE) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    size_t entry_size = TSEDGE_WAL_V3_BATCH_FIXED_SIZE + name_len + count * TSEDGE_WAL_POINT_PAYLOAD_SIZE + TSEDGE_WAL_CHECKSUM_SIZE;
    if (entry_size > UINT32_MAX || entry_size > TSEDGE_WAL_MAX_ENTRY_SIZE) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    *out_size = entry_size;
    return TSEDGE_OK;
}

static int wal_encode_point_v3(const char* name, const tsedge_point* point, uint8_t* entry, size_t capacity, size_t* out_size) {
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > TSEDGE_MAX_SERIES_NAME) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    size_t entry_size = TSEDGE_WAL_V3_POINT_FIXED_SIZE + name_len + TSEDGE_WAL_CHECKSUM_SIZE;
    if (entry_size > UINT32_MAX || entry_size > capacity) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    tsedge_write_u32_le(entry, TSEDGE_WAL_MAGIC);
    tsedge_write_u32_le(entry + 4, TSEDGE_WAL_VERSION_V3);
    tsedge_write_u32_le(entry + 8, (uint32_t)entry_size);
    tsedge_write_u32_le(entry + 12, TSEDGE_WAL_RECORD_POINT);
    tsedge_write_u32_le(entry + 16, (uint32_t)name_len);
    tsedge_write_u64_le(entry + 20, (uint64_t)point->timestamp);

    uint64_t bits = 0;
    memcpy(&bits, &point->value, sizeof(bits));
    tsedge_write_u64_le(entry + 28, bits);
    memcpy(entry + TSEDGE_WAL_V3_POINT_FIXED_SIZE, name, name_len);

    uint32_t checksum = fnv1a32(entry, entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
    tsedge_write_u32_le(entry + entry_size - TSEDGE_WAL_CHECKSUM_SIZE, checksum);

    *out_size = entry_size;
    return TSEDGE_OK;
}

static int wal_encode_batch_v3(const char* name, const tsedge_point* points, size_t count, uint8_t* entry, size_t capacity, size_t* out_size) {
    size_t name_len = strlen(name);
    size_t entry_size = 0;
    int rc = wal_v3_batch_entry_size(name_len, count, &entry_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    if (entry_size > capacity) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    tsedge_write_u32_le(entry, TSEDGE_WAL_MAGIC);
    tsedge_write_u32_le(entry + 4, TSEDGE_WAL_VERSION_V3);
    tsedge_write_u32_le(entry + 8, (uint32_t)entry_size);
    tsedge_write_u32_le(entry + 12, TSEDGE_WAL_RECORD_BATCH);
    tsedge_write_u32_le(entry + 16, (uint32_t)name_len);
    tsedge_write_u32_le(entry + 20, (uint32_t)count);
    memcpy(entry + TSEDGE_WAL_V3_BATCH_FIXED_SIZE, name, name_len);

    uint8_t* cursor = entry + TSEDGE_WAL_V3_BATCH_FIXED_SIZE + name_len;
    for (size_t i = 0; i < count; ++i) {
        tsedge_write_u64_le(cursor, (uint64_t)points[i].timestamp);
        cursor += 8;
        uint64_t bits = 0;
        memcpy(&bits, &points[i].value, sizeof(bits));
        tsedge_write_u64_le(cursor, bits);
        cursor += 8;
    }

    uint32_t checksum = fnv1a32(entry, entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
    tsedge_write_u32_le(entry + entry_size - TSEDGE_WAL_CHECKSUM_SIZE, checksum);

    *out_size = entry_size;
    return TSEDGE_OK;
}

static int wal_write_batch_record(FILE* f, const char* name, const tsedge_point* points, size_t count) {
    /*
     * WAL v3 batch entry format:
     *   u32 magic "TSEW"
     *   u32 version = 3
     *   u32 entry_size
     *   u32 record_type = 2
     *   u32 series_name_length
     *   u32 point_count
     *   bytes series_name
     *   repeated { i64 timestamp, u64 raw double bits }
     *   u32 FNV-1a checksum over all previous entry bytes
     */
    uint8_t* entry = NULL;
    size_t entry_size = 0;
    size_t name_len = strlen(name);
    int rc = wal_v3_batch_entry_size(name_len, count, &entry_size);
    if (rc == TSEDGE_OK) {
        entry = (uint8_t*)malloc(entry_size);
        if (!entry) {
            rc = TSEDGE_ERR_NO_MEMORY;
        }
    }
    if (rc == TSEDGE_OK) {
        rc = wal_encode_batch_v3(name, points, count, entry, entry_size, &entry_size);
    }
    if (rc == TSEDGE_OK) {
        rc = write_exact(f, entry, entry_size);
    }
    free(entry);
    return rc;
}

int tsedge_wal_flush(tsedge_db* db) {
    if (!db) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (db->wal_buffer_size == 0) {
        return TSEDGE_OK;
    }

    FILE* f = fopen(db->wal_path, "ab");
    if (!f) {
        return TSEDGE_ERR_IO;
    }
    int rc = write_exact(f, db->wal_buffer, db->wal_buffer_size);
    if (rc == TSEDGE_OK) {
        rc = wal_sync(f);
    }
    if (fclose(f) != 0 && rc == TSEDGE_OK) {
        rc = TSEDGE_ERR_IO;
    }
    if (rc == TSEDGE_OK) {
        db->wal_buffer_size = 0;
    }
    return rc;
}

static int wal_ensure_buffer_capacity(tsedge_db* db, size_t required) {
    size_t capacity = TSEDGE_WAL_BUFFER_BYTES;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            return TSEDGE_ERR_INVALID_ARGUMENT;
        }
        capacity *= 2u;
    }

    if (db->wal_buffer_capacity >= capacity) {
        return TSEDGE_OK;
    }
    uint8_t* buffer = (uint8_t*)realloc(db->wal_buffer, capacity);
    if (!buffer) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    db->wal_buffer = buffer;
    db->wal_buffer_capacity = capacity;
    return TSEDGE_OK;
}

static int wal_append_bytes(tsedge_db* db, const uint8_t* entry, size_t entry_size) {
    int rc = wal_ensure_buffer_capacity(db, entry_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    size_t threshold = wal_flush_threshold(db);
    if (db->wal_buffer_size > 0 && db->wal_buffer_size + entry_size > threshold) {
        rc = tsedge_wal_flush(db);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }

    if (entry_size > db->wal_buffer_capacity - db->wal_buffer_size) {
        rc = wal_ensure_buffer_capacity(db, db->wal_buffer_size + entry_size);
        if (rc != TSEDGE_OK) {
            return rc;
        }
    }
    memcpy(db->wal_buffer + db->wal_buffer_size, entry, entry_size);
    db->wal_buffer_size += entry_size;
    return TSEDGE_OK;
}

static int wal_append_point_record(tsedge_db* db, const char* series_name, const tsedge_point* point) {
    uint8_t entry[TSEDGE_WAL_MAX_POINT_ENTRY_SIZE];
    size_t entry_size = 0;
    int rc = wal_encode_point_v3(series_name, point, entry, sizeof(entry), &entry_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return wal_append_bytes(db, entry, entry_size);
}

static int wal_append_batch_record(tsedge_db* db, const char* series_name, const tsedge_point* points, size_t count) {
    size_t entry_size = 0;
    int rc = wal_v3_batch_entry_size(strlen(series_name), count, &entry_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    uint8_t* entry = (uint8_t*)malloc(entry_size);
    if (!entry) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    rc = wal_encode_batch_v3(series_name, points, count, entry, entry_size, &entry_size);
    if (rc == TSEDGE_OK) {
        rc = wal_append_bytes(db, entry, entry_size);
    }
    free(entry);
    return rc;
}

int tsedge_wal_append(tsedge_db* db, const char* series_name, const tsedge_point* point) {
    /*
     * The append path calls this before mutating the in-memory buffer. FAST and
     * BALANCED may keep the entry in memory; STRICT flushes it immediately.
     */
    if (!db || !series_name || !point) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = wal_append_point_record(db, series_name, point);
    if (rc == TSEDGE_OK && db->durability == TSEDGE_DURABILITY_STRICT) {
        rc = tsedge_wal_flush(db);
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
    int rc = TSEDGE_OK;
    for (size_t offset = 0; offset < count;) {
        size_t chunk = count - offset;
        if (chunk > TSEDGE_WAL_MAX_BATCH_POINTS) {
            chunk = TSEDGE_WAL_MAX_BATCH_POINTS;
        }
        rc = wal_append_batch_record(db, series_name, points + offset, chunk);
        if (rc != TSEDGE_OK) {
            break;
        }
        offset += chunk;
    }
    if (rc == TSEDGE_OK && db->durability == TSEDGE_DURABILITY_STRICT) {
        rc = tsedge_wal_flush(db);
    }
    return rc;
}

int tsedge_wal_truncate_to_buffers(tsedge_db* db) {
    /*
     * Once a buffer is flushed into a segment block, those points are durable in
     * the segment. The WAL can then be reduced to only the remaining buffers.
     */
    db->wal_buffer_size = 0;

    FILE* f = fopen(db->wal_path, "wb");
    if (!f) {
        return TSEDGE_ERR_IO;
    }

    for (size_t i = 0; i < db->series_count; ++i) {
        tsedge_series* series = &db->series[i];
        for (size_t j = 0; j < series->buffer_count;) {
            size_t chunk = series->buffer_count - j;
            if (chunk > TSEDGE_WAL_MAX_BATCH_POINTS) {
                chunk = TSEDGE_WAL_MAX_BATCH_POINTS;
            }
            int rc = wal_write_batch_record(f, series->name, series->buffer + j, chunk);
            if (rc != TSEDGE_OK) {
                fclose(f);
                return rc;
            }
            j += chunk;
        }
    }

    int rc = wal_sync(f);
    if (fclose(f) != 0 && rc == TSEDGE_OK) {
        rc = TSEDGE_ERR_IO;
    }
    return rc;
}

static int wal_validate_checksum(const uint8_t* entry, size_t entry_size) {
    uint32_t expected_checksum = tsedge_read_u32_le(entry + entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
    uint32_t actual_checksum = fnv1a32(entry, entry_size - TSEDGE_WAL_CHECKSUM_SIZE);
    return expected_checksum == actual_checksum ? TSEDGE_OK : TSEDGE_ERR_CORRUPT;
}

static int wal_copy_series_name(const uint8_t* data, uint32_t name_len, char** out_name) {
    char* name = (char*)malloc((size_t)name_len + 1u);
    if (!name) {
        return TSEDGE_ERR_NO_MEMORY;
    }
    memcpy(name, data, name_len);
    name[name_len] = '\0';

    int rc = tsedge_series_validate_name(name);
    if (rc != TSEDGE_OK) {
        free(name);
        return TSEDGE_ERR_CORRUPT;
    }
    *out_name = name;
    return TSEDGE_OK;
}

static int replay_point_for_name(tsedge_db* db, const char* name, const tsedge_point* point) {
    int rc = tsedge_db_add_series_object(db, name, 1);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, name);
    return tsedge_series_add_recovered_point(db, series, point);
}

static int replay_entry_v2_point(tsedge_db* db, const uint8_t* entry, size_t entry_size) {
    uint32_t name_len = tsedge_read_u32_le(entry + 12);
    if (entry_size < TSEDGE_WAL_V2_MIN_ENTRY_SIZE ||
        name_len == 0 ||
        name_len > TSEDGE_MAX_SERIES_NAME ||
        entry_size != TSEDGE_WAL_V2_POINT_FIXED_SIZE + (size_t)name_len + TSEDGE_WAL_CHECKSUM_SIZE) {
        return TSEDGE_ERR_CORRUPT;
    }

    int rc = wal_validate_checksum(entry, entry_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    char* name = NULL;
    rc = wal_copy_series_name(entry + TSEDGE_WAL_V2_POINT_FIXED_SIZE, name_len, &name);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    tsedge_point point;
    point.timestamp = (int64_t)tsedge_read_u64_le(entry + 16);
    uint64_t bits = tsedge_read_u64_le(entry + 24);
    memcpy(&point.value, &bits, sizeof(point.value));

    rc = replay_point_for_name(db, name, &point);
    free(name);
    return rc;
}

static int replay_entry_v3_point(tsedge_db* db, const uint8_t* entry, size_t entry_size) {
    uint32_t name_len = tsedge_read_u32_le(entry + 16);
    if (entry_size < TSEDGE_WAL_V3_POINT_MIN_ENTRY_SIZE ||
        name_len == 0 ||
        name_len > TSEDGE_MAX_SERIES_NAME ||
        entry_size != TSEDGE_WAL_V3_POINT_FIXED_SIZE + (size_t)name_len + TSEDGE_WAL_CHECKSUM_SIZE) {
        return TSEDGE_ERR_CORRUPT;
    }

    int rc = wal_validate_checksum(entry, entry_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    char* name = NULL;
    rc = wal_copy_series_name(entry + TSEDGE_WAL_V3_POINT_FIXED_SIZE, name_len, &name);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    tsedge_point point;
    point.timestamp = (int64_t)tsedge_read_u64_le(entry + 20);
    uint64_t bits = tsedge_read_u64_le(entry + 28);
    memcpy(&point.value, &bits, sizeof(point.value));

    rc = replay_point_for_name(db, name, &point);
    free(name);
    return rc;
}

static int replay_entry_v3_batch(tsedge_db* db, const uint8_t* entry, size_t entry_size) {
    uint32_t name_len = tsedge_read_u32_le(entry + 16);
    uint32_t point_count = tsedge_read_u32_le(entry + 20);
    size_t expected_size = 0;
    int rc = wal_v3_batch_entry_size(name_len, point_count, &expected_size);
    if (rc != TSEDGE_OK || entry_size < TSEDGE_WAL_V3_BATCH_MIN_ENTRY_SIZE || expected_size != entry_size) {
        return TSEDGE_ERR_CORRUPT;
    }

    rc = wal_validate_checksum(entry, entry_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    char* name = NULL;
    rc = wal_copy_series_name(entry + TSEDGE_WAL_V3_BATCH_FIXED_SIZE, name_len, &name);
    if (rc != TSEDGE_OK) {
        return rc;
    }

    rc = tsedge_db_add_series_object(db, name, 1);
    if (rc != TSEDGE_OK) {
        free(name);
        return rc;
    }
    tsedge_series* series = tsedge_db_find_series(db, name);

    const uint8_t* cursor = entry + TSEDGE_WAL_V3_BATCH_FIXED_SIZE + name_len;
    for (uint32_t i = 0; i < point_count; ++i) {
        tsedge_point point;
        point.timestamp = (int64_t)tsedge_read_u64_le(cursor);
        cursor += 8;
        uint64_t bits = tsedge_read_u64_le(cursor);
        cursor += 8;
        memcpy(&point.value, &bits, sizeof(point.value));
        rc = tsedge_series_add_recovered_point(db, series, &point);
        if (rc != TSEDGE_OK) {
            free(name);
            return rc;
        }
    }

    free(name);
    return TSEDGE_OK;
}

static int wal_validate_entry(const uint8_t* entry, size_t entry_size) {
    uint32_t magic = tsedge_read_u32_le(entry);
    uint32_t version = tsedge_read_u32_le(entry + 4);
    uint32_t stored_entry_size = tsedge_read_u32_le(entry + 8);
    if (magic != TSEDGE_WAL_MAGIC || stored_entry_size != entry_size) {
        return TSEDGE_ERR_CORRUPT;
    }

    if (version == TSEDGE_WAL_VERSION_V2) {
        uint32_t name_len = tsedge_read_u32_le(entry + 12);
        if (entry_size < TSEDGE_WAL_V2_MIN_ENTRY_SIZE ||
            name_len == 0 ||
            name_len > TSEDGE_MAX_SERIES_NAME ||
            entry_size != TSEDGE_WAL_V2_POINT_FIXED_SIZE + (size_t)name_len + TSEDGE_WAL_CHECKSUM_SIZE) {
            return TSEDGE_ERR_CORRUPT;
        }
        return wal_validate_checksum(entry, entry_size);
    }

    if (version != TSEDGE_WAL_VERSION_V3) {
        return TSEDGE_ERR_CORRUPT;
    }

    uint32_t record_type = tsedge_read_u32_le(entry + 12);
    if (record_type == TSEDGE_WAL_RECORD_POINT) {
        uint32_t name_len = tsedge_read_u32_le(entry + 16);
        if (entry_size < TSEDGE_WAL_V3_POINT_MIN_ENTRY_SIZE ||
            name_len == 0 ||
            name_len > TSEDGE_MAX_SERIES_NAME ||
            entry_size != TSEDGE_WAL_V3_POINT_FIXED_SIZE + (size_t)name_len + TSEDGE_WAL_CHECKSUM_SIZE) {
            return TSEDGE_ERR_CORRUPT;
        }
        return wal_validate_checksum(entry, entry_size);
    }

    if (record_type == TSEDGE_WAL_RECORD_BATCH) {
        uint32_t name_len = tsedge_read_u32_le(entry + 16);
        uint32_t point_count = tsedge_read_u32_le(entry + 20);
        size_t expected_size = 0;
        int rc = wal_v3_batch_entry_size(name_len, point_count, &expected_size);
        if (rc != TSEDGE_OK ||
            entry_size < TSEDGE_WAL_V3_BATCH_MIN_ENTRY_SIZE ||
            expected_size != entry_size) {
            return TSEDGE_ERR_CORRUPT;
        }
        return wal_validate_checksum(entry, entry_size);
    }

    return TSEDGE_ERR_CORRUPT;
}

static int replay_entry(tsedge_db* db, const uint8_t* entry, size_t entry_size) {
    uint32_t magic = tsedge_read_u32_le(entry);
    uint32_t version = tsedge_read_u32_le(entry + 4);
    uint32_t stored_entry_size = tsedge_read_u32_le(entry + 8);

    if (magic != TSEDGE_WAL_MAGIC || stored_entry_size != entry_size) {
        return TSEDGE_ERR_CORRUPT;
    }

    if (version == TSEDGE_WAL_VERSION_V2) {
        return replay_entry_v2_point(db, entry, entry_size);
    }
    if (version != TSEDGE_WAL_VERSION_V3) {
        return TSEDGE_ERR_CORRUPT;
    }

    uint32_t record_type = tsedge_read_u32_le(entry + 12);
    if (record_type == TSEDGE_WAL_RECORD_POINT) {
        return replay_entry_v3_point(db, entry, entry_size);
    }
    if (record_type == TSEDGE_WAL_RECORD_BATCH) {
        return replay_entry_v3_batch(db, entry, entry_size);
    }
    return TSEDGE_ERR_CORRUPT;
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
        if (entry_size < TSEDGE_WAL_V2_MIN_ENTRY_SIZE || entry_size > TSEDGE_WAL_MAX_ENTRY_SIZE) {
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
        if (entry_size < TSEDGE_WAL_V2_MIN_ENTRY_SIZE || entry_size > TSEDGE_WAL_MAX_ENTRY_SIZE) {
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

        int valid = wal_validate_entry(entry, entry_size) == TSEDGE_OK;
        free(entry);
        if (!valid) {
            fclose(f);
            return TSEDGE_ERR_CORRUPT;
        }
        ++(*out_entry_count);
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}
