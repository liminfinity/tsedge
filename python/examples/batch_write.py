from pathlib import Path
from tempfile import TemporaryDirectory

from tsedge import Durability, TSEdge


def main() -> None:
    with TemporaryDirectory(prefix="tsedge_py_batch_") as tmp:
        db_path = Path(tmp) / "batch_db"
        with TSEdge.open(db_path) as db:
            db.create_series("air.temperature")
            db.set_durability(Durability.FAST)

            points = [(1710000000000 + i * 1000, 20.0 + (i % 100) * 0.01) for i in range(100_000)]
            db.append_batch("air.temperature", points)
            db.flush_all()

            stats = db.get_series_stats("air.temperature")
            print(f"points: {stats.total_indexed_points + stats.buffered_points}")
            print(f"blocks: {stats.block_count}")
            print(f"segments: {stats.segment_count}")
            print(f"compression_ratio: {stats.compression_ratio:.2f}x")


if __name__ == "__main__":
    main()
