# Quick Start

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

This example creates a database directory named `demo_db`.

The series is named `air.temperature`. Timestamps in the example are simple integers for demonstration. In real systems, timestamps can be Unix time in milliseconds, microseconds or nanoseconds, as long as the application uses one convention consistently.

`read_range` returns matching points, and `aggregate` computes a streaming aggregate over the requested range.
