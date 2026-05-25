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
void tsedge_write_u32_le(uint8_t* dst, uint32_t value);
void tsedge_write_u64_le(uint8_t* dst, uint64_t value);
uint32_t tsedge_read_u32_le(const uint8_t* src);
uint64_t tsedge_read_u64_le(const uint8_t* src);

/*
 * Zigzag maps signed integers to unsigned integers so small negative and
 * positive delta-of-delta values both become compact varints.
 */
uint64_t tsedge_zigzag_encode(int64_t value);
int64_t tsedge_zigzag_decode(uint64_t value);

/* Variable-length integer encoding stores small values in fewer bytes. */
size_t tsedge_varint_size(uint64_t value);
void tsedge_write_varint(uint8_t** cursor, uint64_t value);
int tsedge_read_varint(const uint8_t** cursor, const uint8_t* end, uint64_t* out);

#endif
