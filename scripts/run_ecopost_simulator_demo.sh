#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/build"

echo "Building TSEdge with small segment limit..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release -DTSEDGE_SEGMENT_MAX_BYTES=8192 ..
cmake --build .
ctest --output-on-failure

cat <<EOF

Build is ready.

Terminal 1:
  cd "$BUILD_DIR"
  ./tsedge_ecopost_agent --live --interval-ms 1000

Terminal 2:
  cd "$ROOT_DIR/demo/system-simulator"
  npm install
  TSEDGE_LIVE_OUTPUT=../../build/ecopost_live_output npm run dev

Open:
  http://localhost:3000
EOF
