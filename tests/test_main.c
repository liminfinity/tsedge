#include <stdio.h>

void test_compression_roundtrip(void);
void test_open_close(void);
void test_append_read_aggregate_csv(void);
void test_many_points_blocks_and_reopen(void);
void test_multiple_series(void);
void test_wal_recovery(void);

int tsedge_test_failures = 0;

int main(void) {
    test_open_close();
    test_append_read_aggregate_csv();
    test_many_points_blocks_and_reopen();
    test_multiple_series();
    test_compression_roundtrip();
    test_wal_recovery();

    if (tsedge_test_failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", tsedge_test_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
