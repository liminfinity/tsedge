#ifndef TSEDGE_BITSTREAM_H
#define TSEDGE_BITSTREAM_H

#include "tsedge.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Low-level byte encoding helpers used by block and compression code.
 *
 * These functions are not a database format by themselves; they only provide
 * explicit little-endian integers and compact varints for higher-level modules.
 */
/* Stores a 32-bit integer in explicit little-endian byte order. */
void tsedge_write_u32_le(uint8_t* dst, uint32_t value);

/* Stores a 64-bit integer in explicit little-endian byte order. */
void tsedge_write_u64_le(uint8_t* dst, uint64_t value);

/* Loads a 32-bit integer from little-endian bytes. */
uint32_t tsedge_read_u32_le(const uint8_t* src);

/* Loads a 64-bit integer from little-endian bytes. */
uint64_t tsedge_read_u64_le(const uint8_t* src);

/*
 * Zigzag maps signed integers to unsigned integers so small negative and
 * positive delta-of-delta values both become compact varints.
 */
/* Encodes a signed integer so small deltas become compact unsigned values. */
uint64_t tsedge_zigzag_encode(int64_t value);

/* Decodes a zigzag value back into the original signed integer. */
int64_t tsedge_zigzag_decode(uint64_t value);

/* Returns the number of bytes needed for one varint value. */
size_t tsedge_varint_size(uint64_t value);

/* Writes one varint and advances the caller-owned cursor. */
void tsedge_write_varint(uint8_t** cursor, uint64_t value);

/* Reads one varint bounded by the end pointer. */
int tsedge_read_varint(const uint8_t** cursor, const uint8_t* end, uint64_t* out);

#endif
