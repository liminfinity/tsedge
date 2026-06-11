#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BIN="$BUILD_DIR/tsedge_read_bench"

if [ ! -x "$BIN" ]; then
    echo "tsedge_read_bench not found: $BIN"
    echo "Build first: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
    exit 1
fi

run_case() {
    title="$1"
    shift
    echo
    echo "== $title =="
    "$BIN" "$@"
}

run_case "window aggregation, 1M, 1 series, 100 windows" \
    --points 1000000 --series 1 --scenario window_aggregate --target-windows 100 --repeat 5

run_case "window aggregation, 1M, 1 series, 1000 windows" \
    --points 1000000 --series 1 --scenario window_aggregate --target-windows 1000 --repeat 5

run_case "window aggregation, 1M, 1 series, 10000 windows" \
    --points 1000000 --series 1 --scenario window_aggregate --target-windows 10000 --repeat 5

run_case "window aggregation, 1M, 6 series, 1000 windows" \
    --points 1000000 --series 6 --scenario window_aggregate --target-windows 1000 --repeat 5
