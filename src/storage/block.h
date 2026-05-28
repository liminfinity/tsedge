#ifndef TSEDGE_BLOCK_H
#define TSEDGE_BLOCK_H

#include "tsedge.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TSEDGE_BLOCK_MAX_POINTS 4096u
#define TSEDGE_BLOCK_COMPRESSION_DELTA_XOR 1u
#define TSEDGE_BLOCK_HEADER_SIZE 72u

/*
 * Metadata stored before every compressed block in a segment file.
 *
 * min_timestamp and max_timestamp allow range reads to skip blocks that cannot
 * overlap the requested interval. payload_size is the compressed byte count that
 * makes skipping possible without decoding.
 */
typedef struct {
    /* Number of points encoded in this block. */
    uint32_t point_count;

    /* Inclusive timestamp bounds for fast range filtering. */
    int64_t min_timestamp;
    int64_t max_timestamp;

    /* Separate compressed payload sizes for timestamp and value streams. */
    uint32_t timestamp_size;
    uint32_t value_size;
    uint32_t payload_size;

    /* Identifies the lossless compression scheme used by the payload. */
    uint32_t compression_type;

    /*
     * Precomputed aggregate statistics. When a query fully covers this block,
     * aggregation can use these values without decompressing the payload.
     */
    double min_value;
    double max_value;
    double sum_value;
} tsedge_block_header;

typedef struct {
    /*
     * segment_id selects the segment file and offset selects the block inside
     * that file. Together they let range reads and aggregates jump directly to
     * candidate blocks after the index is rebuilt at open.
     */
    uint32_t segment_id;
    long offset;
    int64_t min_timestamp;
    int64_t max_timestamp;
    uint32_t point_count;
    uint32_t payload_size;
} tsedge_block_index_entry;

int tsedge_block_write_header(FILE* f, const tsedge_block_header* header);
int tsedge_block_read_header(FILE* f, tsedge_block_header* header, bool* eof);
int tsedge_block_write_payload(FILE* f, const uint8_t* timestamp_data, size_t timestamp_size, const uint8_t* value_data, size_t value_size);
int tsedge_block_read_payload(FILE* f, const tsedge_block_header* header, uint8_t** out_timestamp_data, uint8_t** out_value_data);
int tsedge_block_skip_payload(FILE* f, const tsedge_block_header* header);

#endif
