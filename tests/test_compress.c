#include "tsedge.h"
#include "compress.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int tsedge_test_failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++tsedge_test_failures; \
        return; \
    } \
} while (0)

#define CHECK_OK(expr) do { \
    int _rc = (expr); \
    if (_rc != TSEDGE_OK) { \
        fprintf(stderr, "CHECK_OK failed at %s:%d: %s -> %s\n", __FILE__, __LINE__, #expr, tsedge_strerror(_rc)); \
        ++tsedge_test_failures; \
        return; \
    } \
} while (0)

static uint64_t double_bits(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void test_compression_roundtrip(void) {
    /*
     * Roundtrip tests prove that compression is lossless. The value set includes
     * cases where numeric comparison is not enough, such as -0.0 and NaN.
     */
    tsedge_point points[8] = {
        {1000, 1.0},
        {1010, 1.0},
        {1020, -0.0},
        {1035, NAN},
        {1055, 123.456},
        {1080, 123.456},
        {1110, INFINITY},
        {1145, -42.0},
    };

    uint8_t* ts_data = NULL;
    uint8_t* value_data = NULL;
    size_t ts_size = 0;
    size_t value_size = 0;
    int64_t timestamps[8];
    double values[8];

    CHECK_OK(tsedge_compress_timestamps(points, 8, &ts_data, &ts_size));
    CHECK_OK(tsedge_decompress_timestamps(ts_data, ts_size, 8, timestamps));
    for (size_t i = 0; i < 8; ++i) {
        CHECK(timestamps[i] == points[i].timestamp);
    }

    CHECK_OK(tsedge_compress_values(points, 8, &value_data, &value_size));
    CHECK_OK(tsedge_decompress_values(value_data, value_size, 8, values));
    for (size_t i = 0; i < 8; ++i) {
        /* Double values must be restored bit-for-bit, not just numerically. */
        CHECK(double_bits(values[i]) == double_bits(points[i].value));
    }

    free(ts_data);
    free(value_data);
}
