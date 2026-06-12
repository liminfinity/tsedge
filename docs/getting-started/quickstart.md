# Quick Start

This example creates a local database directory, writes three points, reads them back and computes an average.

```python
from tsedge import TSEdge, Aggregate, Durability

with TSEdge.open("demo_db") as db:
    db.create_series("air.temperature")
    db.set_durability(Durability.BALANCED)

    db.append_batch(
        "air.temperature",
        [(1, 10.0), (2, 20.0), (3, 30.0)],
    )

    points = db.read_range("air.temperature", 1, 4)
    avg = db.aggregate("air.temperature", 1, 4, Aggregate.AVG)

    print(points)
    print(avg)
```

When the code runs, TSEdge creates a directory named `demo_db`. That directory contains the database files: manifest, WAL and series segment files.

The series is named `air.temperature`. A TSEdge point always contains one timestamp and one floating-point value. If you have several physical measurements, store them as separate series such as `air.temperature`, `air.humidity` and `motor.current`.

Timestamps in this example are simple integers. In real systems they are usually Unix timestamps in milliseconds, microseconds or nanoseconds. TSEdge does not enforce a unit, so choose one convention and use it consistently.

`read_range` returns copied points from the requested range. `aggregate` computes the result while scanning data and does not need to allocate a large result array.
