#!/usr/bin/env sh
set -eu

POINTS="${1:-1000000}"
RESULTS_DIR="benchmark_results"

mkdir -p "$RESULTS_DIR"

echo "writing benchmark results to: $RESULTS_DIR"

if [ ! -x ./build/tsedge_bench ]; then
    echo "missing ./build/tsedge_bench; build the project first" >&2
    exit 1
fi

if [ ! -x ./build/file_bench ]; then
    echo "missing ./build/file_bench; build the project first" >&2
    exit 1
fi

./build/tsedge_bench "$POINTS" "$RESULTS_DIR/tsedge.csv"
./build/file_bench "$POINTS" "$RESULTS_DIR/file.csv"

if [ -x ./build/sqlite_bench ]; then
    ./build/sqlite_bench "$POINTS" "$RESULTS_DIR/sqlite.csv"
else
    echo "sqlite_bench not found; skipping SQLite benchmark"
fi

echo "saved:"
echo "  $RESULTS_DIR/tsedge.csv"
echo "  $RESULTS_DIR/file.csv"
if [ -f "$RESULTS_DIR/sqlite.csv" ]; then
    echo "  $RESULTS_DIR/sqlite.csv"
fi
