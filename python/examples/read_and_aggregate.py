from pathlib import Path
from tempfile import TemporaryDirectory

from tsedge import TSEdge


def main() -> None:
    with TemporaryDirectory(prefix="tsedge_py_read_") as tmp:
        db_path = Path(tmp) / "read_db"
        with TSEdge.open(db_path) as db:
            db.create_series("motor.temperature")
            db.append_batch("motor.temperature", [(i, float(i % 50)) for i in range(10_000)])

            points = db.read_range("motor.temperature", 100, 110)
            min_value = db.aggregate("motor.temperature", 0, 9999, "min")
            max_value = db.aggregate("motor.temperature", 0, 9999, "max")
            avg_value = db.aggregate("motor.temperature", 0, 9999, "avg")
            count_value = db.aggregate("motor.temperature", 0, 9999, "count")

            print(f"range_points: {len(points)}")
            print(f"min: {min_value:.2f}")
            print(f"max: {max_value:.2f}")
            print(f"avg: {avg_value:.2f}")
            print(f"count: {int(count_value)}")


if __name__ == "__main__":
    main()
