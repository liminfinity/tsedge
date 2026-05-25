#define _POSIX_C_SOURCE 200809L

#include "block.h"
#include "bitstream.h"

#include <limits.h>
#include <stdlib.h>

/*
 * Magic and version make accidental or future-incompatible files fail fast
 * instead of being decoded as valid blocks.
 */
#define TSEDGE_BLOCK_MAGIC 0x42455354u
#define TSEDGE_BLOCK_VERSION 1u
#define TSEDGE_BLOCK_HEADER_SIZE 48u

static int write_exact(FILE* f, const void* data, size_t size) {
    return fwrite(data, 1, size, f) == size ? TSEDGE_OK : TSEDGE_ERR_IO;
}

static int read_exact(FILE* f, void* data, size_t size) {
    return fread(data, 1, size, f) == size ? TSEDGE_OK : TSEDGE_ERR_IO;
}

int tsedge_block_write_header(FILE* f, const tsedge_block_header* header) {
    if (!f || !header || header->point_count == 0) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    /* Header fields are encoded explicitly as little-endian bytes. */
    uint8_t buf[TSEDGE_BLOCK_HEADER_SIZE];
    tsedge_write_u32_le(buf, TSEDGE_BLOCK_MAGIC);
    tsedge_write_u32_le(buf + 4, TSEDGE_BLOCK_VERSION);
    tsedge_write_u32_le(buf + 8, header->point_count);
    tsedge_write_u32_le(buf + 12, header->compression_type);
    tsedge_write_u64_le(buf + 16, (uint64_t)header->min_timestamp);
    tsedge_write_u64_le(buf + 24, (uint64_t)header->max_timestamp);
    tsedge_write_u32_le(buf + 32, header->timestamp_size);
    tsedge_write_u32_le(buf + 36, header->value_size);
    tsedge_write_u32_le(buf + 40, header->payload_size);
    tsedge_write_u32_le(buf + 44, 0);
    return write_exact(f, buf, sizeof(buf));
}

int tsedge_block_read_header(FILE* f, tsedge_block_header* header, bool* eof) {
    if (!f || !header || !eof) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    uint8_t buf[TSEDGE_BLOCK_HEADER_SIZE];
    size_t n = fread(buf, 1, sizeof(buf), f);
    if (n == 0 && feof(f)) {
        *eof = true;
        return TSEDGE_OK;
    }
    *eof = false;
    if (n != sizeof(buf)) {
        return TSEDGE_ERR_CORRUPT;
    }

    uint32_t magic = tsedge_read_u32_le(buf);
    uint32_t version = tsedge_read_u32_le(buf + 4);
    if (magic != TSEDGE_BLOCK_MAGIC || version != TSEDGE_BLOCK_VERSION) {
        return TSEDGE_ERR_CORRUPT;
    }

    header->point_count = tsedge_read_u32_le(buf + 8);
    header->compression_type = tsedge_read_u32_le(buf + 12);
    header->min_timestamp = (int64_t)tsedge_read_u64_le(buf + 16);
    header->max_timestamp = (int64_t)tsedge_read_u64_le(buf + 24);
    header->timestamp_size = tsedge_read_u32_le(buf + 32);
    header->value_size = tsedge_read_u32_le(buf + 36);
    header->payload_size = tsedge_read_u32_le(buf + 40);

    /*
     * The payload is trusted only if metadata is internally consistent. Segment
     * scans use payload_size to skip non-overlapping blocks without decoding.
     */
    if (header->point_count == 0 ||
        header->compression_type != TSEDGE_BLOCK_COMPRESSION_DELTA_XOR ||
        header->payload_size != header->timestamp_size + header->value_size) {
        return TSEDGE_ERR_CORRUPT;
    }
    return TSEDGE_OK;
}

int tsedge_block_write_payload(FILE* f, const uint8_t* timestamp_data, size_t timestamp_size, const uint8_t* value_data, size_t value_size) {
    if (!f || (!timestamp_data && timestamp_size > 0) || (!value_data && value_size > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    int rc = write_exact(f, timestamp_data, timestamp_size);
    if (rc != TSEDGE_OK) {
        return rc;
    }
    return write_exact(f, value_data, value_size);
}

int tsedge_block_read_payload(FILE* f, const tsedge_block_header* header, uint8_t** out_timestamp_data, uint8_t** out_value_data) {
    if (!f || !header || !out_timestamp_data || !out_value_data) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    *out_timestamp_data = NULL;
    *out_value_data = NULL;
    uint8_t* timestamp_data = (uint8_t*)malloc(header->timestamp_size);
    uint8_t* value_data = (uint8_t*)malloc(header->value_size);
    if (!timestamp_data || !value_data) {
        free(timestamp_data);
        free(value_data);
        return TSEDGE_ERR_NO_MEMORY;
    }

    int rc = read_exact(f, timestamp_data, header->timestamp_size);
    if (rc == TSEDGE_OK) {
        rc = read_exact(f, value_data, header->value_size);
    }
    if (rc != TSEDGE_OK) {
        free(timestamp_data);
        free(value_data);
        return rc;
    }

    *out_timestamp_data = timestamp_data;
    *out_value_data = value_data;
    return TSEDGE_OK;
}

int tsedge_block_skip_payload(FILE* f, const tsedge_block_header* header) {
    if (!f || !header) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    return fseek(f, (long)header->payload_size, SEEK_CUR) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}
