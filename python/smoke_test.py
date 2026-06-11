from pathlib import Path
import random
import sys
from tempfile import TemporaryDirectory

from tsedge import Aggregate, Durability, TSEdge, TSEdgeError, verify_database


EXAMPLES_DIR = Path(__file__).resolve().parent / "examples"
sys.path.insert(0, str(EXAMPLES_DIR))

import sensor_simulation  # noqa: E402


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def smoke_sensor_simulation_module(tmp: str) -> None:
    rng = random.Random(42)
    batch = sensor_simulation.generate_batch("air.temperature", 0, 10, sensor_simulation.STEP_MS, rng)
    assert_equal(len(batch), 10, "sensor batch count")
    if not all(batch[i][0] < batch[i + 1][0] for i in range(len(batch) - 1)):
        raise AssertionError("sensor timestamps are not increasing")
    if not all(isinstance(value, float) for _timestamp, value in batch):
        raise AssertionError("sensor values are not floats")

    sensor_simulation.run_simulation(
        Path(tmp) / "sensor_simulation_db",
        Path(tmp) / "sensor_export.csv",
        points_per_series=1000,
        batch_size=100,
    )


def main() -> None:
    with TemporaryDirectory(prefix="tsedge_py_smoke_") as tmp:
        db_path = Path(tmp) / "smoke_db"
        csv_path = Path(tmp) / "out.csv"

        with TSEdge.open(db_path) as db:
            db.set_durability("balanced")
            db.create_series("air.temperature")
            db.append("air.temperature", 1, 10.0)
            db.append_batch("air.temperature", [(2, 20.0), (3, 30.0)])
            db.append_batch("air.temperature", [])

            handle = db.get_series_handle("air.temperature")
            db.append_handle(handle, 4, 40.0)
            db.append_batch_handle(handle, [(5, 50.0), (6, 60.0)])

            points = db.read_range("air.temperature", 1, 6)
            assert_equal(len(points), 6, "read_range count")
            assert_equal(points[0].timestamp, 1, "first timestamp")
            assert_equal(points[-1].value, 60.0, "last value")

            avg = db.aggregate("air.temperature", 1, 6, Aggregate.AVG)
            assert_equal(avg, 35.0, "avg")

            windows = db.aggregate_windowed("air.temperature", 1, 7, 3)
            assert_equal(len(windows), 2, "window count")
            assert_equal(windows[0].count, 3, "first window count")

            stats = db.get_series_stats("air.temperature")
            if stats.buffered_points == 0 and stats.total_indexed_points == 0:
                raise AssertionError("stats did not report points")

            series = db.list_series()
            assert_equal([item.name for item in series], ["air.temperature"], "series list")

            db.export_csv("air.temperature", 1, 6, csv_path)
            if not csv_path.exists():
                raise AssertionError("CSV export did not create a file")

            report = db.verify()
            assert_equal(report.error_count, 0, "verify error_count")

            try:
                db.read_range("missing.series", 0, 1)
            except TSEdgeError:
                pass
            else:
                raise AssertionError("missing series did not raise")

            try:
                db.set_disk_quota(100 * 1024 * 1024)
                assert_equal(db.get_disk_quota(), 100 * 1024 * 1024, "quota")
                db.enforce_disk_quota()
            except TSEdgeError as exc:
                if "quota API is not available" not in exc.message:
                    raise

        report = verify_database(db_path)
        assert_equal(report.error_count, 0, "verify_path error_count")
        smoke_sensor_simulation_module(tmp)

    print("python smoke test passed")


if __name__ == "__main__":
    main()
