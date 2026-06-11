from __future__ import annotations

import argparse
import math
import random
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

from tsedge import Aggregate, Durability, TSEdge, TSEdgeError, WindowAggregate


START_TIMESTAMP = 1_710_000_000_000
STEP_MS = 1000
POINTS_PER_SERIES = 100_000
BATCH_SIZE = 1_000
SERIES = (
    "air.temperature",
    "air.humidity",
    "motor.vibration",
    "motor.current",
)


def value_for_series(series: str, index: int, rng: random.Random) -> float:
    if series == "air.temperature":
        daily_wave = math.sin(index / 3600.0) * 3.0
        slow_drift = math.sin(index / 20000.0) * 0.8
        noise = rng.uniform(-0.05, 0.05)
        return 22.0 + daily_wave + slow_drift + noise

    if series == "air.humidity":
        wave = math.cos(index / 2400.0) * 8.0
        noise = rng.uniform(-0.4, 0.4)
        return 48.0 + wave + noise

    if series == "motor.vibration":
        baseline = 0.18 + math.sin(index / 110.0) * 0.03
        spike = 1.4 if index > 0 and index % 17_000 == 0 else 0.0
        noise = rng.uniform(-0.015, 0.015)
        return baseline + spike + noise

    if series == "motor.current":
        step = 2.5 if (index // 15_000) % 2 else 0.0
        ripple = math.sin(index / 37.0) * 0.12
        noise = rng.uniform(-0.03, 0.03)
        return 6.0 + step + ripple + noise

    raise ValueError(f"unknown series: {series}")


def generate_batch(series: str, start_index: int, count: int, step_ms: int, rng: random.Random) -> List[Tuple[int, float]]:
    points = []
    for offset in range(count):
        index = start_index + offset
        timestamp = START_TIMESTAMP + index * step_ms
        points.append((timestamp, float(value_for_series(series, index, rng))))
    return points


def format_float(value: float | None, digits: int = 3) -> str:
    if value is None:
        return "n/a"
    return f"{value:.{digits}f}"


def print_window(label: str, window: WindowAggregate) -> None:
    print(
        f"  {label}: start={window.window_start}, end={window.window_end}, "
        f"count={window.count}, min={window.min:.3f}, max={window.max:.3f}, avg={window.avg:.3f}"
    )


def ensure_series(db: TSEdge, series_names: Sequence[str]) -> None:
    existing = {item.name for item in db.list_series()}
    for series in series_names:
        if series not in existing:
            db.create_series(series)


def write_sensor_data(db: TSEdge, points_per_series: int, batch_size: int) -> None:
    rngs: Dict[str, random.Random] = {
        series: random.Random(42 + index * 1000)
        for index, series in enumerate(SERIES)
    }

    print(f"Writing {points_per_series} points per series in batches of {batch_size}...")
    for series in SERIES:
        written = 0
        while written < points_per_series:
            count = min(batch_size, points_per_series - written)
            batch = generate_batch(series, written, count, STEP_MS, rngs[series])
            db.append_batch(series, batch)
            written += count
        print(f"  {series}: done")
    db.flush_all()


def print_series_statistics(db: TSEdge) -> None:
    print("\nSeries statistics:")
    for series in SERIES:
        stats = db.get_series_stats(series)
        points = stats.total_indexed_points + stats.buffered_points
        print(f"  {series}:")
        print(f"    points: {points}")
        print(f"    segments: {stats.segment_count}")
        print(f"    blocks: {stats.block_count}")
        print(f"    compressed size: {stats.compressed_size_bytes} bytes")
        print(f"    compression ratio: {format_float(stats.compression_ratio, 2)}x")
        print(f"    bytes per point: {format_float(stats.bytes_per_point, 3)}")


def print_aggregates(db: TSEdge, start_ts: int, end_ts: int) -> None:
    print("\nAggregates:")
    for series in SERIES:
        min_value = db.aggregate(series, start_ts, end_ts, Aggregate.MIN)
        max_value = db.aggregate(series, start_ts, end_ts, Aggregate.MAX)
        avg_value = db.aggregate(series, start_ts, end_ts, Aggregate.AVG)
        count_value = db.aggregate(series, start_ts, end_ts, Aggregate.COUNT)
        print(
            f"  {series}: min={min_value:.3f}, max={max_value:.3f}, "
            f"avg={avg_value:.3f}, count={int(count_value)}"
        )


def print_window_aggregation(db: TSEdge, points_per_series: int) -> None:
    end_exclusive = START_TIMESTAMP + points_per_series * STEP_MS
    windows = db.aggregate_windowed(
        "air.temperature",
        START_TIMESTAMP,
        end_exclusive,
        window_size=60_000,
    )
    total_count = sum(window.count for window in windows)
    reduction = (total_count / len(windows)) if windows else 0.0

    print("\nWindow aggregation:")
    print(f"  source points: {total_count}")
    print(f"  windows: {len(windows)}")
    print(f"  reduction: {reduction:.1f}:1")
    if windows:
        print_window("first", windows[0])
        print_window("last ", windows[-1])


def print_first_points(db: TSEdge) -> None:
    points = db.read_range("air.temperature", START_TIMESTAMP, START_TIMESTAMP + 9 * STEP_MS)
    print("\nFirst 10 temperature points:")
    for point in points[:10]:
        print(f"  {point.timestamp} -> {point.value:.3f}")


def export_temperature_csv(db: TSEdge, csv_path: Path, points_per_series: int) -> None:
    export_count = min(1000, points_per_series)
    end_ts = START_TIMESTAMP + (export_count - 1) * STEP_MS
    db.export_csv("air.temperature", START_TIMESTAMP, end_ts, csv_path)
    print(f"\nCSV export written to {csv_path}")


def print_verify_report(db: TSEdge) -> None:
    report = db.verify()
    print("\nVerify:")
    print(f"  series: {report.series_count}")
    print(f"  segments: {report.segment_count}")
    print(f"  blocks: {report.block_count}")
    print(f"  WAL entries: {report.wal_entry_count}")
    print(f"  errors: {report.error_count}")
    if report.error_count > 0:
        print(f"  first error path: {report.first_error_path}")
        print(f"  first error message: {report.first_error_message}")
        raise SystemExit(1)


def run_simulation(db_path: Path, csv_path: Path, points_per_series: int, batch_size: int) -> None:
    start_ts = START_TIMESTAMP
    end_ts = START_TIMESTAMP + (points_per_series - 1) * STEP_MS

    with TSEdge.open(db_path) as db:
        db.set_durability(Durability.BALANCED)
        try:
            db.set_disk_quota(256 * 1024 * 1024)
        except TSEdgeError as exc:
            if "quota API is not available" not in exc.message:
                raise

        ensure_series(db, SERIES)
        write_sensor_data(db, points_per_series, batch_size)
        print_series_statistics(db)
        print_aggregates(db, start_ts, end_ts)
        print_window_aggregation(db, points_per_series)
        print_first_points(db)
        export_temperature_csv(db, csv_path, points_per_series)
        print_verify_report(db)

    print("\nSimulation completed successfully.")
    print(
        "This example shows a typical edge workflow: Python generates sensor data, "
        "TSEdge stores it through the C library, and compact range queries / "
        "aggregates are available afterwards."
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Simulate an edge device writing sensor telemetry to TSEdge.")
    parser.add_argument("--db", default="sensor_simulation_db", help="database directory")
    parser.add_argument("--csv", default="sensor_export.csv", help="CSV export path")
    parser.add_argument("--points", type=int, default=POINTS_PER_SERIES, help="points per series")
    parser.add_argument("--batch-size", type=int, default=BATCH_SIZE, help="append_batch size")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.points <= 0:
        raise SystemExit("--points must be positive")
    if args.batch_size <= 0:
        raise SystemExit("--batch-size must be positive")
    run_simulation(Path(args.db), Path(args.csv), args.points, args.batch_size)


if __name__ == "__main__":
    main()
