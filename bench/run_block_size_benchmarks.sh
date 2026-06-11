#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
POINTS="${POINTS:-1000000}"
REPEAT="${REPEAT:-5}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT_DIR}"
RESULTS="$SCRIPT_DIR/block_size_results.csv"

get_field() {
    key="$1"
    file="$2"
    awk -F': ' -v key="$key" '$1 == key { print $2; exit }' "$file"
}

run_and_show() {
    output_file="$1"
    shift
    "$@" > "$output_file"
    cat "$output_file"
}

printf "%s\n" "block_size_points,write_batch_fast_points_per_second,write_append_fast_points_per_second,read_tiny_query_seconds,read_small_query_seconds,read_medium_query_seconds,read_full_points_per_second,aggregate_avg_full_query_seconds,aggregate_min_max_full_query_seconds,compression_ratio,bytes_per_point,segment_count,block_count,tiny_blocks_decoded,tiny_points_decoded,small_blocks_decoded,small_points_decoded" > "$RESULTS"

for block_size in 1024 4096 8192 16384 32768; do
    build_dir="$BUILD_ROOT/build-bs-$block_size"
    echo
    echo "== block size: $block_size points =="
    cmake -S "$ROOT_DIR" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DTSEDGE_BLOCK_MAX_POINTS="$block_size"
    cmake --build "$build_dir" --target tsedge_write_bench tsedge_read_bench

    batch_out="/tmp/tsedge_block_size_${block_size}_write_batch.out"
    append_out="/tmp/tsedge_block_size_${block_size}_write_append.out"
    read_tiny_out="/tmp/tsedge_block_size_${block_size}_read_tiny.out"
    read_small_out="/tmp/tsedge_block_size_${block_size}_read_small.out"
    read_medium_out="/tmp/tsedge_block_size_${block_size}_read_medium.out"
    read_full_out="/tmp/tsedge_block_size_${block_size}_read_full.out"
    avg_full_out="/tmp/tsedge_block_size_${block_size}_avg_full.out"
    minmax_full_out="/tmp/tsedge_block_size_${block_size}_minmax_full.out"

    echo
    echo "-- write batch fast --"
    run_and_show "$batch_out" "$build_dir/tsedge_write_bench" --points "$POINTS" --mode batch --batch-size 1000 --series 1 --durability fast

    echo
    echo "-- write append fast --"
    run_and_show "$append_out" "$build_dir/tsedge_write_bench" --points "$POINTS" --mode append --series 1 --durability fast

    echo
    echo "-- read tiny --"
    run_and_show "$read_tiny_out" "$build_dir/tsedge_read_bench" --points "$POINTS" --series 1 --scenario read_range_tiny --repeat "$REPEAT"

    echo
    echo "-- read small --"
    run_and_show "$read_small_out" "$build_dir/tsedge_read_bench" --points "$POINTS" --series 1 --scenario read_range_small --repeat "$REPEAT"

    echo
    echo "-- read medium --"
    run_and_show "$read_medium_out" "$build_dir/tsedge_read_bench" --points "$POINTS" --series 1 --scenario read_range_medium --repeat "$REPEAT"

    echo
    echo "-- read full --"
    run_and_show "$read_full_out" "$build_dir/tsedge_read_bench" --points "$POINTS" --series 1 --scenario read_range_full --repeat "$REPEAT"

    echo
    echo "-- aggregate avg full --"
    run_and_show "$avg_full_out" "$build_dir/tsedge_read_bench" --points "$POINTS" --series 1 --scenario aggregate_avg_full --repeat "$REPEAT"

    echo
    echo "-- aggregate min/max full --"
    run_and_show "$minmax_full_out" "$build_dir/tsedge_read_bench" --points "$POINTS" --series 1 --scenario aggregate_min_max_full --repeat "$REPEAT"

    printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n" \
        "$block_size" \
        "$(get_field points_per_second "$batch_out")" \
        "$(get_field points_per_second "$append_out")" \
        "$(get_field query_seconds "$read_tiny_out")" \
        "$(get_field query_seconds "$read_small_out")" \
        "$(get_field query_seconds "$read_medium_out")" \
        "$(get_field points_per_second "$read_full_out")" \
        "$(get_field query_seconds "$avg_full_out")" \
        "$(get_field query_seconds "$minmax_full_out")" \
        "$(get_field compression_ratio "$batch_out")" \
        "$(get_field bytes_per_point "$batch_out")" \
        "$(get_field segment_count "$batch_out")" \
        "$(get_field block_count "$batch_out")" \
        "$(get_field blocks_decoded "$read_tiny_out")" \
        "$(get_field points_decoded "$read_tiny_out")" \
        "$(get_field blocks_decoded "$read_small_out")" \
        "$(get_field points_decoded "$read_small_out")" >> "$RESULTS"
done

echo
echo "CSV written: $RESULTS"
cat "$RESULTS"
