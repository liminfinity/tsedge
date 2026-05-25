#ifndef TSEDGE_COMPRESS_H
#define TSEDGE_COMPRESS_H

#include "tsedge.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Lossless timestamp stream compression.
 *
 * The first timestamp is stored as a base value, the first delta is stored
 * directly, and later deltas are represented as delta-of-delta varints.
 */
int tsedge_compress_timestamps(const tsedge_point* points, size_t count, uint8_t** out, size_t* out_size);
int tsedge_decompress_timestamps(const uint8_t* data, size_t size, size_t count, int64_t* out);

/*
 * Lossless double stream compression.
 *
 * The first double is stored as raw IEEE-754 bits. Each next value is encoded
 * by XORing its bits with the previous value. Decompression must reconstruct
 * the exact original bit pattern, including -0.0 and NaN payloads.
 */
int tsedge_compress_values(const tsedge_point* points, size_t count, uint8_t** out, size_t* out_size);
int tsedge_decompress_values(const uint8_t* data, size_t size, size_t count, double* out);

#endif
