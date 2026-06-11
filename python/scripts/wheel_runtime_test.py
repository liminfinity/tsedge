#!/usr/bin/env python3
from __future__ import annotations

import os
import tempfile

from tsedge import Aggregate, Durability, TSEdge


def main() -> None:
    os.environ.pop("TSEDGE_LIBRARY", None)

    db_path = tempfile.mkdtemp(prefix="tsedge_cibw_")
    db = TSEdge.open(db_path)
    try:
        db.create_series("air.temperature")
        db.set_durability(Durability.BALANCED)
        db.append_batch(
            "air.temperature",
            [(1, 10.0), (2, 20.0), (3, 30.0)],
        )

        points = db.read_range("air.temperature", 1, 4)
        avg = db.aggregate("air.temperature", 1, 4, Aggregate.AVG)
        report = db.verify()

        assert len(points) == 3
        assert abs(avg - 20.0) < 1e-9
        assert report.error_count == 0
    finally:
        db.close()

    print("cibuildwheel runtime test OK")


if __name__ == "__main__":
    main()
