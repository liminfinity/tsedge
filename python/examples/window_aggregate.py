from pathlib import Path
from tempfile import TemporaryDirectory

from tsedge import TSEdge


def main() -> None:
    with TemporaryDirectory(prefix="tsedge_py_window_") as tmp:
        db_path = Path(tmp) / "window_db"
        with TSEdge.open(db_path) as db:
            db.create_series("air.temperature")
            db.append_batch("air.temperature", [(i, 20.0 + (i % 100) * 0.1) for i in range(100_000)])

            windows = db.aggregate_windowed("air.temperature", 0, 100_000, 1000)
            print(f"windows: {len(windows)}")
            if windows:
                print(f"first: {windows[0]}")
                print(f"last: {windows[-1]}")


if __name__ == "__main__":
    main()
