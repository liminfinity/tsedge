# TSEdge Benchmark Notebook

This directory contains `benchmark_analysis.ipynb`, a Jupyter Notebook for
comparing benchmark results from TSEdge, raw binary files, CSV files, and SQLite
when SQLite results are available.

## 1. Build The Project

From the repository root:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
ctest --output-on-failure
cd ..
```

## 2. Run Benchmarks

Use the helper script:

```bash
sh scripts/run_benchmarks.sh 1000000
```

Or run the benchmark binaries manually:

```bash
mkdir -p benchmark_results
./build/tsedge_bench 1000000 benchmark_results/tsedge.csv
./build/file_bench 1000000 benchmark_results/file.csv
./build/sqlite_bench 1000000 benchmark_results/sqlite.csv
```

`sqlite_bench` is optional and exists only when CMake finds SQLite3.

## 3. Python Environment

Create a local virtual environment from the repository root:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

You can also use the helper script:

```bash
sh scripts/setup_notebook_env.sh
source .venv/bin/activate
```

The virtual environment directory `.venv/` is ignored by Git.

## 4. Python Dependencies

The notebook uses lightweight dependencies:

- `pandas`
- `matplotlib`
- `jupyter`
- `notebook`
- `ipykernel`
- standard library modules: `pathlib`, `subprocess`, `platform`, `datetime`

Dependencies are listed in the root `requirements.txt` file. The project does
not require seaborn, plotly, scipy, or other heavy plotting packages.

## 5. Open The Notebook

```bash
jupyter notebook notebooks/benchmark_analysis.ipynb
```

or:

```bash
jupyter lab notebooks/benchmark_analysis.ipynb
```

Or use:

```bash
sh scripts/run_notebook.sh
```

## 6. Generated Files

The notebook reads:

- `benchmark_results/tsedge.csv`
- `benchmark_results/file.csv`
- `benchmark_results/sqlite.csv`

It generates:

- `benchmark_results/figures/*.png`
- `benchmark_results/figures/*.svg`
- `benchmark_results/diploma_main_table.csv`
- `benchmark_results/diploma_relative_table.csv`
- `benchmark_results/diploma_dataset_table.csv`

`benchmark_results/` is ignored by Git because it contains generated data.
