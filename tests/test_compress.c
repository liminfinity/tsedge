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

static double bits_to_double(uint64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void check_value_roundtrip(const tsedge_point* points, size_t count) {
    uint8_t* value_data = NULL;
    size_t value_size = 0;
    double* values = (double*)malloc(count * sizeof(*values));
    CHECK(values != NULL);

    CHECK_OK(tsedge_compress_values(points, count, &value_data, &value_size));
    CHECK_OK(tsedge_decompress_values(value_data, value_size, count, values));
    for (size_t i = 0; i < count; ++i) {
        CHECK(double_bits(values[i]) == double_bits(points[i].value));
    }

    free(values);
    free(value_data);
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

    tsedge_point repeated[16];
    tsedge_point smooth[16];
    tsedge_point negative[16];
    for (size_t i = 0; i < 16; ++i) {
        repeated[i].timestamp = (int64_t)i;
        repeated[i].value = 42.0;
        smooth[i].timestamp = (int64_t)i;
        smooth[i].value = 100.0 + (double)i * 0.125;
        negative[i].timestamp = (int64_t)i;
        negative[i].value = -100.0 - (double)i * 0.5;
    }
    check_value_roundtrip(repeated, 16);
    check_value_roundtrip(smooth, 16);
    check_value_roundtrip(negative, 16);

    tsedge_point zeroes[2] = {
        {0, 0.0},
        {1, -0.0},
    };
    check_value_roundtrip(zeroes, 2);

    tsedge_point* random_points = (tsedge_point*)malloc(100000u * sizeof(*random_points));
    CHECK(random_points != NULL);
    uint64_t state = 0x123456789abcdef0ull;
    for (size_t i = 0; i < 100000u; ++i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        if ((state & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) {
            state ^= 0x0010000000000000ull;
        }
        random_points[i].timestamp = (int64_t)i;
        random_points[i].value = bits_to_double(state);
    }
    check_value_roundtrip(random_points, 100000u);
    free(random_points);
}
