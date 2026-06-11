#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BENCH="${1:-$ROOT_DIR/build/tsedge_write_bench}"

if [ ! -x "$BENCH" ]; then
    echo "tsedge_write_bench not found: $BENCH"
    echo "Build first: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
    exit 1
fi

run_case() {
    title="$1"
    shift
    echo
    echo "== $title =="
    "$BENCH" "$@" --db-path /tmp/tsedge_write_bench_db
}

run_case "append, 100k, 1 series" \
    --points 100000 --mode append --series 1

run_case "append handle, 100k, 1 series" \
    --points 100000 --mode append_handle --series 1

run_case "append fast, 1M, 1 series" \
    --points 1000000 --mode append --series 1 --durability fast

run_case "append balanced, 1M, 1 series" \
    --points 1000000 --mode append --series 1 --durability balanced

run_case "append strict, 1M, 1 series" \
    --points 1000000 --mode append --series 1 --durability strict

run_case "append handle fast, 1M, 1 series" \
    --points 1000000 --mode append_handle --series 1 --durability fast

run_case "append handle fast, 1M, 6 series" \
    --points 1000000 --mode append_handle --series 6 --durability fast

run_case "batch fast 1000, 1M, 1 series" \
    --points 1000000 --mode batch --batch-size 1000 --series 1 --durability fast

run_case "batch balanced 1000, 1M, 1 series" \
    --points 1000000 --mode batch --batch-size 1000 --series 1 --durability balanced

run_case "batch strict 1000, 1M, 1 series" \
    --points 1000000 --mode batch --batch-size 1000 --series 1 --durability strict

run_case "batch fast 10000, 1M, 1 series" \
    --points 1000000 --mode batch --batch-size 10000 --series 1 --durability fast

run_case "batch handle fast 1000, 1M, 1 series" \
    --points 1000000 --mode batch_handle --batch-size 1000 --series 1 --durability fast
