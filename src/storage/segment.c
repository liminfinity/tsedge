#define _POSIX_C_SOURCE 200809L

#include "segment.h"
#include "block.h"
#include "compress.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void aggregate_state_add(tsedge_aggregate_state* state, double value) {
    if (state->count == 0) {
        state->min_value = value;
        state->max_value = value;
    }
    if (value < state->min_value) {
        state->min_value = value;
    }
    if (value > state->max_value) {
        state->max_value = value;
    }
    state->sum_value += value;
    ++state->count;
}

static void aggregate_state_add_block(tsedge_aggregate_state* state, const tsedge_block_header* header) {
    if (state->count == 0) {
        state->min_value = header->min_value;
        state->max_value = header->max_value;
    }
    if (header->min_value < state->min_value) {
        state->min_value = header->min_value;
    }
    if (header->max_value > state->max_value) {
        state->max_value = header->max_value;
    }
    state->sum_value += header->sum_value;
    state->count += header->point_count;
}

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

static int block_intersects_header(const tsedge_block_header* header, int64_t from_timestamp, int64_t to_timestamp) {
    return header->max_timestamp >= from_timestamp && header->min_timestamp <= to_timestamp;
}

static int block_intersects_index(const tsedge_block_index_entry* entry, int64_t from_timestamp, int64_t to_timestamp) {
    return entry->max_timestamp >= from_timestamp && entry->min_timestamp <= to_timestamp;
}

static int block_fully_covered(const tsedge_block_header* header, int64_t from_timestamp, int64_t to_timestamp) {
    return header->min_timestamp >= from_timestamp && header->max_timestamp <= to_timestamp;
}

static void fill_index_entry(uint32_t segment_id, long offset, const tsedge_block_header* header, tsedge_block_index_entry* entry) {
    entry->segment_id = segment_id;
    entry->offset = offset;
    entry->min_timestamp = header->min_timestamp;
    entry->max_timestamp = header->max_timestamp;
    entry->point_count = header->point_count;
    entry->payload_size = header->payload_size;
}

static int append_index_entry(tsedge_block_index_entry** entries, size_t* count, size_t* capacity, uint32_t segment_id, long offset, const tsedge_block_header* header) {
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 16u : *capacity * 2u;
        tsedge_block_index_entry* resized = (tsedge_block_index_entry*)realloc(*entries, next * sizeof(**entries));
        if (!resized) {
            return TSEDGE_ERR_NO_MEMORY;
        }
        *entries = resized;
        *capacity = next;
    }
    fill_index_entry(segment_id, offset, header, &(*entries)[*count]);
    ++(*count);
    return TSEDGE_OK;
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

int tsedge_segment_scan_index(const char* segment_path, uint32_t segment_id, tsedge_block_index_entry** out_entries, size_t* out_count) {
    if (!segment_path || !out_entries || !out_count) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    *out_entries = NULL;
    *out_count = 0;

    FILE* f = fopen(segment_path, "rb");
    if (!f) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    tsedge_block_index_entry* entries = NULL;
    size_t count = 0;
    size_t capacity = 0;
    for (;;) {
        long offset = ftell(f);
        if (offset < 0) {
            free(entries);
            fclose(f);
            return TSEDGE_ERR_IO;
        }

        tsedge_block_header header;
        bool eof = false;
        int rc = tsedge_block_read_header(f, &header, &eof);
        if (rc != TSEDGE_OK) {
            free(entries);
            fclose(f);
            return rc;
        }
        if (eof) {
            break;
        }

        rc = append_index_entry(&entries, &count, &capacity, segment_id, offset, &header);
        if (rc == TSEDGE_OK) {
            rc = tsedge_block_skip_payload(f, &header);
        }
        if (rc != TSEDGE_OK) {
            free(entries);
            fclose(f);
            return rc;
        }
    }

    if (fclose(f) != 0) {
        free(entries);
        return TSEDGE_ERR_IO;
    }
    *out_entries = entries;
    *out_count = count;
    return TSEDGE_OK;
}

static int decode_block(
    FILE* f,
    const tsedge_block_header* header,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data,
    bool* out_stopped
) {
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
                    *out_stopped = true;
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

static int aggregate_partial_block(FILE* f, const tsedge_block_header* header, int64_t from_timestamp, int64_t to_timestamp, tsedge_aggregate_state* state) {
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
                aggregate_state_add(state, values[i]);
            }
        }
    }

    free(timestamp_data);
    free(value_data);
    free(timestamps);
    free(values);
    return rc;
}

int tsedge_segment_read_range(
    const char* segment_path,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data,
    bool* out_stopped
) {
    if (!segment_path || !callback || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (out_stopped) {
        *out_stopped = false;
    }

    FILE* f = fopen(segment_path, "rb");
    if (!f) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    /*
     * Sequential fallback for callers without an index. Non-overlapping blocks
     * are skipped using metadata, avoiding unnecessary decompression.
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
        if (!block_intersects_header(&header, from_timestamp, to_timestamp)) {
            rc = tsedge_block_skip_payload(f, &header);
        } else {
            bool stopped = false;
            rc = decode_block(f, &header, from_timestamp, to_timestamp, callback, user_data, &stopped);
            if (stopped) {
                if (out_stopped) {
                    *out_stopped = true;
                }
                fclose(f);
                return rc;
            }
        }
        if (rc != TSEDGE_OK) {
            fclose(f);
            return rc;
        }
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

int tsedge_segment_read_range_indexed(
    const char* segment_path,
    const tsedge_block_index_entry* entries,
    size_t entry_count,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_point_callback callback,
    void* user_data,
    bool* out_stopped
) {
    if (!segment_path || (!entries && entry_count > 0) || !callback || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (out_stopped) {
        *out_stopped = false;
    }
    if (entry_count == 0) {
        return TSEDGE_OK;
    }

    FILE* f = fopen(segment_path, "rb");
    if (!f) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    for (size_t i = 0; i < entry_count; ++i) {
        if (!block_intersects_index(&entries[i], from_timestamp, to_timestamp)) {
            continue;
        }
        if (fseek(f, entries[i].offset, SEEK_SET) != 0) {
            fclose(f);
            return TSEDGE_ERR_IO;
        }

        tsedge_block_header header;
        bool eof = false;
        int rc = tsedge_block_read_header(f, &header, &eof);
        if (rc != TSEDGE_OK || eof) {
            fclose(f);
            return eof ? TSEDGE_ERR_CORRUPT : rc;
        }

        bool stopped = false;
        rc = decode_block(f, &header, from_timestamp, to_timestamp, callback, user_data, &stopped);
        if (rc != TSEDGE_OK) {
            fclose(f);
            return rc;
        }
        if (stopped) {
            if (out_stopped) {
                *out_stopped = true;
            }
            fclose(f);
            return TSEDGE_OK;
        }
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}

int tsedge_segment_aggregate(
    const char* segment_path,
    const tsedge_block_index_entry* entries,
    size_t entry_count,
    int64_t from_timestamp,
    int64_t to_timestamp,
    tsedge_aggregate_state* state
) {
    if (!segment_path || (!entries && entry_count > 0) || !state || from_timestamp > to_timestamp) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (entry_count == 0) {
        return TSEDGE_OK;
    }

    FILE* f = fopen(segment_path, "rb");
    if (!f) {
        return errno == ENOENT ? TSEDGE_OK : TSEDGE_ERR_IO;
    }

    for (size_t i = 0; i < entry_count; ++i) {
        if (!block_intersects_index(&entries[i], from_timestamp, to_timestamp)) {
            continue;
        }
        if (fseek(f, entries[i].offset, SEEK_SET) != 0) {
            fclose(f);
            return TSEDGE_ERR_IO;
        }

        tsedge_block_header header;
        bool eof = false;
        int rc = tsedge_block_read_header(f, &header, &eof);
        if (rc != TSEDGE_OK || eof) {
            fclose(f);
            return eof ? TSEDGE_ERR_CORRUPT : rc;
        }

        if (block_fully_covered(&header, from_timestamp, to_timestamp)) {
            rc = tsedge_block_skip_payload(f, &header);
            if (rc == TSEDGE_OK) {
                aggregate_state_add_block(state, &header);
            }
        } else {
            rc = aggregate_partial_block(f, &header, from_timestamp, to_timestamp, state);
        }
        if (rc != TSEDGE_OK) {
            fclose(f);
            return rc;
        }
    }

    return fclose(f) == 0 ? TSEDGE_OK : TSEDGE_ERR_IO;
}
