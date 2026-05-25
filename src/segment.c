#define _POSIX_C_SOURCE 200809L

#include "segment.h"
#include "block.h"
#include "compress.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int make_header(const tsedge_point* points, size_t count, size_t timestamp_size, size_t value_size, tsedge_block_header* header) {
    if (count == 0 || count > UINT32_MAX || timestamp_size > UINT32_MAX || value_size > UINT32_MAX) {
        return TSEDGE_ERR_INTERNAL;
    }
    /* The timestamp bounds are stored so reads can skip unrelated blocks. */
    int64_t min_ts = points[0].timestamp;
    int64_t max_ts = points[0].timestamp;
    for (size_t i = 1; i < count; ++i) {
        if (points[i].timestamp < min_ts) {
            min_ts = points[i].timestamp;
        }
        if (points[i].timestamp > max_ts) {
            max_ts = points[i].timestamp;
        }
    }
    header->point_count = (uint32_t)count;
    header->min_timestamp = min_ts;
    header->max_timestamp = max_ts;
    header->timestamp_size = (uint32_t)timestamp_size;
    header->value_size = (uint32_t)value_size;
    header->payload_size = (uint32_t)(timestamp_size + value_size);
    header->compression_type = TSEDGE_BLOCK_COMPRESSION_DELTA_XOR;
    return TSEDGE_OK;
}

int tsedge_segment_append_block(const char* segment_path, const tsedge_point* points, size_t count) {
    if (!segment_path || (!points && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return TSEDGE_OK;
    }

    /*
     * Segment append turns a memory buffer into one block: compression prepares
     * payload bytes, block.c serializes the metadata and payload.
     */
    uint8_t* timestamp_data = NULL;
    uint8_t* value_data = NULL;
    size_t timestamp_size = 0;
    size_t value_size = 0;
    int rc = tsedge_compress_timestamps(points, count, &timestamp_data, &timestamp_size);
    if (rc == TSEDGE_OK) {
        rc = tsedge_compress_values(points, count, &value_data, &value_size);
    }
    if (rc != TSEDGE_OK) {
        free(timestamp_data);
        free(value_data);
        return rc;
    }

    tsedge_block_header header;
    rc = make_header(points, count, timestamp_size, value_size, &header);
    if (rc != TSEDGE_OK) {
        free(timestamp_data);
        free(value_data);
        return rc;
    }

    FILE* f = fopen(segment_path, "ab");
    if (!f) {
        free(timestamp_data);
        free(value_data);
        return TSEDGE_ERR_IO;
    }
    rc = tsedge_block_write_header(f, &header);
    if (rc == TSEDGE_OK) {
        rc = tsedge_block_write_payload(f, timestamp_data, timestamp_size, value_data, value_size);
    }
    if (fclose(f) != 0 && rc == TSEDGE_OK) {
        rc = TSEDGE_ERR_IO;
    }
    free(timestamp_data);
    free(value_data);
    return rc;
}

static int decode_and_emit(FILE* f, const tsedge_block_header* header, int64_t from_timestamp, int64_t to_timestamp, tsedge_point_callback callback, void* user_data) {
    /*
     * Only blocks that passed the timestamp-bound filter are decoded. Points are
     * streamed to the caller callback instead of stored in a query result array.
     */
    uint8_t* timestamp_data = NULL;
    uint8_t* value_data = NULL;
    int64_t* timestamps = (int64_t*)malloc((size_t)header->point_count * sizeof(*timestamps));
    double* values = (double*)malloc((size_t)header->point_count * sizeof(*values));
    if (!timestamps || !values) {
        free(timestamps);
        free(values);
        return TSEDGE_ERR_NO_MEMORY;
    }

    int rc = tsedge_block_read_payload(f, header, &timestamp_data, &value_data);
    if (rc == TSEDGE_OK) {
        rc = tsedge_decompress_timestamps(timestamp_data, header->timestamp_size, header->point_count, timestamps);
    }
    if (rc == TSEDGE_OK) {
        rc = tsedge_decompress_values(value_data, header->value_size, header->point_count, values);
    }
    if (rc == TSEDGE_OK) {
        for (uint32_t i = 0; i < header->point_count; ++i) {
            if (timestamps[i] >= from_timestamp && timestamps[i] <= to_timestamp) {
                tsedge_point point;
                point.timestamp = timestamps[i];
                point.value = values[i];
                if (callback(&point, user_data) != 0) {
                    break;
                }
            }
        }
    }

    free(timestamp_data);
    free(value_data);
    free(timestamps);
    free(values);
    return rc;
}

static int block_intersects(const tsedge_block_header* header, int64_t from_timestamp, int64_t to_timestamp) {
    return header->max_timestamp >= from_timestamp && header->min_timestamp <= to_timestamp;
}

int tsedge_segment_read_range(const char* segment_path, int64_t from_timestamp, int64_t to_timestamp, tsedge_point_callback callback, void* user_data) {
    if (!segment_path || !callback || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    FILE* f = fopen(segment_path, "rb");
    if (!f) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    /*
     * Blocks are read sequentially from the append-only segment. Non-overlapping
     * blocks are skipped using metadata, avoiding unnecessary decompression.
     */
    for (;;) {
        tsedge_block_header header;
        bool eof = false;
        int rc = tsedge_block_read_header(f, &header, &eof);
        if (rc != TSEDGE_OK) {
            fclose(f);
            return rc;
        }
        if (eof) {
            break;
        }
        if (!block_intersects(&header, from_timestamp, to_timestamp)) {
            rc = tsedge_block_skip_payload(f, &header);
        } else {
            rc = decode_and_emit(f, &header, from_timestamp, to_timestamp, callback, user_data);
        }
        if (rc != TSEDGE_OK) {
            fclose(f);
            return rc;
        }
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}
