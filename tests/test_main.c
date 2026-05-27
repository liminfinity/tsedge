#include <stdio.h>

void test_compression_roundtrip(void);
void test_block_header_validation(void);
void test_open_close(void);
void test_append_read_aggregate_csv(void);
void test_append_batch_zero_one_many(void);
void test_append_batch_multiple_blocks(void);
void test_series_stats_empty_and_buffered(void);
void test_series_stats_after_single_block_flush(void);
void test_series_stats_blocks_and_reopen(void);
void test_many_points_blocks_and_reopen(void);
void test_block_stats_aggregate_and_index(void);
void test_read_range_callback_stops_full_scan(void);
void test_multiple_series(void);
void test_wal_recovery(void);
void test_wal_empty_replay(void);
void test_wal_incomplete_last_entry(void);
void test_wal_bad_checksum(void);
void test_wal_recovery_after_batch_append(void);

int tsedge_test_failures = 0;

int main(void) {
    test_open_close();
    test_append_read_aggregate_csv();
    test_append_batch_zero_one_many();
    test_append_batch_multiple_blocks();
    test_series_stats_empty_and_buffered();
    test_series_stats_after_single_block_flush();
    test_series_stats_blocks_and_reopen();
    test_many_points_blocks_and_reopen();
    test_block_stats_aggregate_and_index();
    test_read_range_callback_stops_full_scan();
    test_multiple_series();
    test_compression_roundtrip();
    test_block_header_validation();
    test_wal_recovery();
    test_wal_empty_replay();
    test_wal_incomplete_last_entry();
    test_wal_bad_checksum();
    test_wal_recovery_after_batch_append();

    if (tsedge_test_failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", tsedge_test_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
