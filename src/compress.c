#include "compress.h"
#include "bitstream.h"

#include <stdlib.h>
#include <string.h>

static uint64_t double_to_bits(double value) {
    /* memcpy avoids aliasing undefined behavior while preserving exact bits. */
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double bits_to_double(uint64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

int tsedge_compress_timestamps(const tsedge_point* points, size_t count, uint8_t** out, size_t* out_size) {
    if ((!points && count > 0) || !out || !out_size) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    *out = NULL;
    *out_size = 0;
    if (count == 0) {
        return TSEDGE_OK;
    }

    /*
     * Timestamp compression is lossless and streaming-friendly: store a base
     * timestamp, then encode how the interval between timestamps changes.
     */
    size_t size = 8;
    if (count >= 2) {
        size += 8;
        int64_t prev_delta = points[1].timestamp - points[0].timestamp;
        for (size_t i = 2; i < count; ++i) {
            int64_t delta = points[i].timestamp - points[i - 1].timestamp;
            int64_t dod = delta - prev_delta;
            size += tsedge_varint_size(tsedge_zigzag_encode(dod));
            prev_delta = delta;
        }
    }

    uint8_t* data = (uint8_t*)malloc(size);
    if (!data) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    uint8_t* cursor = data;
    tsedge_write_u64_le(cursor, (uint64_t)points[0].timestamp);
    cursor += 8;

    if (count >= 2) {
        int64_t prev_delta = points[1].timestamp - points[0].timestamp;
        tsedge_write_u64_le(cursor, (uint64_t)prev_delta);
        cursor += 8;
        for (size_t i = 2; i < count; ++i) {
            int64_t delta = points[i].timestamp - points[i - 1].timestamp;
            int64_t dod = delta - prev_delta;
            tsedge_write_varint(&cursor, tsedge_zigzag_encode(dod));
            prev_delta = delta;
        }
    }

    *out = data;
    *out_size = size;
    return TSEDGE_OK;
}

int tsedge_decompress_timestamps(const uint8_t* data, size_t size, size_t count, int64_t* out) {
    if ((!data && count > 0) || (!out && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return TSEDGE_OK;
    }
    if (size < 8) {
        return TSEDGE_ERR_CORRUPT;
    }

    /*
     * Decode mirrors the encoder: first timestamp, first delta, then
     * accumulated delta-of-delta values.
     */
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    out[0] = (int64_t)tsedge_read_u64_le(cursor);
    cursor += 8;

    if (count >= 2) {
        if ((size_t)(end - cursor) < 8) {
            return TSEDGE_ERR_CORRUPT;
        }
        int64_t prev_delta = (int64_t)tsedge_read_u64_le(cursor);
        cursor += 8;
        out[1] = out[0] + prev_delta;

        for (size_t i = 2; i < count; ++i) {
            uint64_t encoded = 0;
            int rc = tsedge_read_varint(&cursor, end, &encoded);
            if (rc != TSEDGE_OK) {
                return rc;
            }
            int64_t delta = prev_delta + tsedge_zigzag_decode(encoded);
            out[i] = out[i - 1] + delta;
            prev_delta = delta;
        }
    }

    return cursor == end ? TSEDGE_OK : TSEDGE_ERR_CORRUPT;
}

int tsedge_compress_values(const tsedge_point* points, size_t count, uint8_t** out, size_t* out_size) {
    if ((!points && count > 0) || !out || !out_size) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }

    *out = NULL;
    *out_size = 0;
    if (count == 0) {
        return TSEDGE_OK;
    }

    /*
     * This simplified Gorilla-inspired scheme stores the first double as raw
     * bits and then stores XOR differences from the previous value. It is not
     * intended to outperform Gorilla; correctness is the goal here.
     */
    size_t size = 8;
    uint64_t prev = double_to_bits(points[0].value);
    for (size_t i = 1; i < count; ++i) {
        uint64_t current = double_to_bits(points[i].value);
        uint64_t xor_value = prev ^ current;
        size += xor_value == 0 ? 1 : 9;
        prev = current;
    }

    uint8_t* data = (uint8_t*)malloc(size);
    if (!data) {
        return TSEDGE_ERR_NO_MEMORY;
    }

    uint8_t* cursor = data;
    prev = double_to_bits(points[0].value);
    tsedge_write_u64_le(cursor, prev);
    cursor += 8;

    for (size_t i = 1; i < count; ++i) {
        uint64_t current = double_to_bits(points[i].value);
        uint64_t xor_value = prev ^ current;
        if (xor_value == 0) {
            *cursor++ = 0;
        } else {
            *cursor++ = 1;
            tsedge_write_u64_le(cursor, xor_value);
            cursor += 8;
        }
        prev = current;
    }

    *out = data;
    *out_size = size;
    return TSEDGE_OK;
}

int tsedge_decompress_values(const uint8_t* data, size_t size, size_t count, double* out) {
    if ((!data && count > 0) || (!out && count > 0)) {
        return TSEDGE_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return TSEDGE_OK;
    }
    if (size < 8) {
        return TSEDGE_ERR_CORRUPT;
    }

    /*
     * Values are reconstructed as uint64_t bit patterns before converting back
     * to double, preserving exact bit-level roundtrip behavior.
     */
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    uint64_t prev = tsedge_read_u64_le(cursor);
    cursor += 8;
    out[0] = bits_to_double(prev);

    for (size_t i = 1; i < count; ++i) {
        if (cursor >= end) {
            return TSEDGE_ERR_CORRUPT;
        }
        uint8_t marker = *cursor++;
        uint64_t current = prev;
        if (marker == 1) {
            if ((size_t)(end - cursor) < 8) {
                return TSEDGE_ERR_CORRUPT;
            }
            current = prev ^ tsedge_read_u64_le(cursor);
            cursor += 8;
        } else if (marker != 0) {
            return TSEDGE_ERR_CORRUPT;
        }
        out[i] = bits_to_double(current);
        prev = current;
    }

    return cursor == end ? TSEDGE_OK : TSEDGE_ERR_CORRUPT;
}
