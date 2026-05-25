#!/usr/bin/env sh
set -eu

VENV_DIR="${VENV_DIR:-.venv}"

if [ ! -x "$VENV_DIR/bin/jupyter" ]; then
    echo "Jupyter was not found in $VENV_DIR." >&2
    echo "Run scripts/setup_notebook_env.sh first." >&2
    exit 1
fi

# shellcheck disable=SC1091
. "$VENV_DIR/bin/activate"

jupyter notebook notebooks/benchmark_analysis.ipynb
