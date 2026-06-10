#!/bin/sh
set -u

if [ "$#" -ne 2 ]; then
    echo "usage: run_crash_recovery_demo.sh <writer> <checker>" >&2
    exit 1
fi

WRITER="$1"
CHECKER="$2"
DB_PATH="${TMPDIR:-/tmp}/tsedge_crash_recovery_demo_$$"

cleanup() {
    rm -rf "$DB_PATH"
}

trap cleanup EXIT INT TERM
rm -rf "$DB_PATH"

set +e
"$WRITER" "$DB_PATH"
WRITER_RC=$?
set -e

if [ "$WRITER_RC" -eq 0 ]; then
    echo "expected crash writer to fail" >&2
    exit 1
fi

"$CHECKER" "$DB_PATH"
