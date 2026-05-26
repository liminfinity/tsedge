#include "block.h"
#include "bitstream.h"
#include "tsedge.h"

#include <stdio.h>
#include <stdint.h>
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

static void write_double_le(uint8_t* out, double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    tsedge_write_u64_le(out, bits);
}

static void make_header_bytes(uint8_t buf[72]) {
    tsedge_write_u32_le(buf, 0x42455354u);
    tsedge_write_u32_le(buf + 4, 2u);
    tsedge_write_u32_le(buf + 8, 1u);
    tsedge_write_u32_le(buf + 12, TSEDGE_BLOCK_COMPRESSION_DELTA_XOR);
    tsedge_write_u64_le(buf + 16, 10u);
    tsedge_write_u64_le(buf + 24, 20u);
    tsedge_write_u32_le(buf + 32, 8u);
    tsedge_write_u32_le(buf + 36, 8u);
    tsedge_write_u32_le(buf + 40, 16u);
    write_double_le(buf + 44, 1.0);
    write_double_le(buf + 52, 1.0);
    write_double_le(buf + 60, 1.0);
    tsedge_write_u32_le(buf + 68, 0u);
}

static int read_header_from_bytes(uint8_t buf[72]) {
    FILE* f = tmpfile();
    if (!f) {
        return TSEDGE_ERR_IO;
    }
    if (fwrite(buf, 1, 72, f) != 72) {
        fclose(f);
        return TSEDGE_ERR_IO;
    }
    rewind(f);
    tsedge_block_header header;
    bool eof = false;
    int rc = tsedge_block_read_header(f, &header, &eof);
    fclose(f);
    return rc;
}

void test_block_header_validation(void) {
    uint8_t buf[72];

    make_header_bytes(buf);
    CHECK_OK(read_header_from_bytes(buf));

    make_header_bytes(buf);
    tsedge_write_u32_le(buf, 0x12345678u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u32_le(buf + 4, 99u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u32_le(buf + 8, 0u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u64_le(buf + 16, 30u);
    tsedge_write_u64_le(buf + 24, 20u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u32_le(buf + 40, 99u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u32_le(buf + 12, 99u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u32_le(buf + 8, TSEDGE_BLOCK_MAX_POINTS + 1u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u32_le(buf + 32, 0u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u32_le(buf + 36, 0u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);

    make_header_bytes(buf);
    tsedge_write_u32_le(buf + 68, 1u);
    CHECK(read_header_from_bytes(buf) == TSEDGE_ERR_CORRUPT);
}
