#include "bitstream.h"

void tsedge_write_u32_le(uint8_t* dst, uint32_t value) {
    /* Binary files are explicit little-endian, independent of host byte order. */
    for (int i = 0; i < 4; ++i) {
        dst[i] = (uint8_t)((value >> (i * 8)) & 0xffu);
    }
}

void tsedge_write_u64_le(uint8_t* dst, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        dst[i] = (uint8_t)((value >> (i * 8)) & 0xffu);
    }
}

uint32_t tsedge_read_u32_le(const uint8_t* src) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= ((uint32_t)src[i]) << (i * 8);
    }
    return value;
}

uint64_t tsedge_read_u64_le(const uint8_t* src) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= ((uint64_t)src[i]) << (i * 8);
    }
    return value;
}

uint64_t tsedge_zigzag_encode(int64_t value) {
    /* Put sign information in the low bit so nearby signed values stay small. */
    return ((uint64_t)value << 1) ^ (uint64_t)(value >> 63);
}

int64_t tsedge_zigzag_decode(uint64_t value) {
    return (int64_t)((value >> 1) ^ (uint64_t)(-(int64_t)(value & 1u)));
}

size_t tsedge_varint_size(uint64_t value) {
    size_t size = 1;
    while (value >= 0x80u) {
        value >>= 7;
        ++size;
    }
    return size;
}

void tsedge_write_varint(uint8_t** cursor, uint64_t value) {
    /* Seven payload bits per byte; the high bit means another byte follows. */
    while (value >= 0x80u) {
        **cursor = (uint8_t)((value & 0x7fu) | 0x80u);
        ++(*cursor);
        value >>= 7;
    }
    **cursor = (uint8_t)value;
    ++(*cursor);
}

int tsedge_read_varint(const uint8_t** cursor, const uint8_t* end, uint64_t* out) {
    uint64_t value = 0;
    unsigned shift = 0;
    while (*cursor < end && shift <= 63) {
        uint8_t byte = **cursor;
        ++(*cursor);
        value |= ((uint64_t)(byte & 0x7fu)) << shift;
        if ((byte & 0x80u) == 0) {
            *out = value;
            return TSEDGE_OK;
        }
        shift += 7;
    }
    return TSEDGE_ERR_CORRUPT;
}
