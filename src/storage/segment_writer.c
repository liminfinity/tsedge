#define _POSIX_C_SOURCE 200809L

#include "segment_writer.h"
#include "block.h"
#include "compress.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int make_header(const tsedge_point* points, size_t count, size_t timestamp_size, size_t value_size, tsedge_block_header* header) {
    if (count == 0 || count > UINT32_MAX || timestamp_size > UINT32_MAX || value_size > UINT32_MAX) {
        return TSEDGE_ERR_INTERNAL;
    }
    if (timestamp_size + value_size > UINT32_MAX) {
        return TSEDGE_ERR_INTERNAL;
    }

    /*
     * Timestamp bounds are stored so reads can skip unrelated blocks. Value
     * statistics let aggregate queries consume fully covered blocks without
     * paying the decompression cost.
     */
    int64_t min_ts = points[0].timestamp;
    int64_t max_ts = points[0].timestamp;
    double min_value = points[0].value;
    double max_value = points[0].value;
    double sum_value = points[0].value;
    for (size_t i = 1; i < count; ++i) {
        if (points[i].timestamp < min_ts) {
            min_ts = points[i].timestamp;
        }
        if (points[i].timestamp > max_ts) {
            max_ts = points[i].timestamp;
        }
        if (points[i].value < min_value) {
            min_value = points[i].value;
        }
        if (points[i].value > max_value) {
            max_value = points[i].value;
        }
        sum_value += points[i].value;
    }

    header->point_count = (uint32_t)count;
    header->min_timestamp = min_ts;
    header->max_timestamp = max_ts;
    header->timestamp_size = (uint32_t)timestamp_size;
    header->value_size = (uint32_t)value_size;
    header->payload_size = (uint32_t)(timestamp_size + value_size);
    header->compression_type = TSEDGE_BLOCK_COMPRESSION_DELTA_XOR;
    header->min_value = min_value;
    header->max_value = max_value;
    header->sum_value = sum_value;
    return TSEDGE_OK;
}

static void fill_index_entry(uint32_t segment_id, long offset, const tsedge_block_header* header, tsedge_block_index_entry* entry) {
    entry->segment_id = segment_id;
    entry->offset = offset;
    entry->min_timestamp = header->min_timestamp;
    entry->max_timestamp = header->max_timestamp;
    entry->point_count = header->point_count;
    entry->payload_size = header->payload_size;
}

int tsedge_segment_estimate_block_size(const tsedge_point* points, size_t count, uint64_t* out_size) {
    if ((!points && count > 0) || !out_size) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    *out_size = 0;
    if (count == 0) {
        return TSEDGE_OK;
    }

    uint8_t* timestamp_data = NULL;
    uint8_t* value_data = NULL;
    size_t timestamp_size = 0;
    size_t value_size = 0;
    int rc = tsedge_compress_timestamps(points, count, &timestamp_data, &timestamp_size);
    if (rc == TSEDGE_OK) {
        rc = tsedge_compress_values(points, count, &value_data, &value_size);
    }
    if (rc == TSEDGE_OK) {
        *out_size = (uint64_t)TSEDGE_BLOCK_HEADER_SIZE + (uint64_t)timestamp_size + (uint64_t)value_size;
    }
    free(timestamp_data);
    free(value_data);
    return rc;
}

int tsedge_segment_append_block(
    const char* segment_path,
    uint32_t segment_id,
    const tsedge_point* points,
    size_t count,
    tsedge_block_index_entry* out_index_entry
) {
    if (!segment_path || (!points && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return TSEDGE_OK;
    }

    /*
     * Segment append turns a memory buffer into one block: compression prepares
     * payload bytes, block.c serializes metadata and payload, and the caller can
     * remember the resulting offset in its in-memory index.
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
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        free(timestamp_data);
        free(value_data);
        return TSEDGE_ERR_IO;
    }
    long offset = ftell(f);
    if (offset < 0) {
        fclose(f);
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

    if (rc == TSEDGE_OK && out_index_entry) {
        fill_index_entry(segment_id, offset, &header, out_index_entry);
    }
    return rc;
}
