from pathlib import Path
from tempfile import TemporaryDirectory

from tsedge import Aggregate, TSEdge


def main() -> None:
    with TemporaryDirectory(prefix="tsedge_py_basic_") as tmp:
        db_path = Path(tmp) / "sensor_db"
        with TSEdge.open(db_path) as db:
            db.create_series("air.temperature")
            db.append("air.temperature", 1710000000000, 23.5)
            db.append("air.temperature", 1710000001000, 23.6)
            db.append("air.temperature", 1710000002000, 23.7)

            points = db.read_range("air.temperature", 1710000000000, 1710000003000)
            avg = db.aggregate("air.temperature", 1710000000000, 1710000003000, Aggregate.AVG)

            print(f"points: {len(points)}")
            print(f"avg: {avg:.2f}")


if __name__ == "__main__":
    main()
