# Python API

The `tsedge` Python package is a `ctypes` wrapper around the embedded C API.

## Main classes and enums

- `TSEdge`: database wrapper.
- `Point`: copied point returned by range reads.
- `WindowAggregate`: one windowed aggregate result.
- `Aggregate`: `MIN`, `MAX`, `SUM`, `AVG`, `COUNT`.
- `Durability`: `FAST`, `BALANCED`, `STRICT`.
- `TSEdgeError`: raised when a C API call returns an error status.

## Opening a database

```python
from tsedge import TSEdge

db = TSEdge.open("sensor_db")
db.close()
```

`TSEdge` also supports context manager usage:

```python
with TSEdge.open("sensor_db") as db:
    db.create_series("air.temperature")
```

## Writing data

```python
from tsedge import Durability, TSEdge

with TSEdge.open("sensor_db") as db:
    db.create_series("air.temperature")
    db.set_durability(Durability.BALANCED)
    db.append("air.temperature", 1, 10.0)
    db.append_batch("air.temperature", [(2, 20.0), (3, 30.0)])
```

For high-throughput writes, resolve a series handle once:

```python
handle = db.get_series_handle("air.temperature")
db.append_handle(handle, 4, 40.0)
db.append_batch_handle(handle, [(5, 50.0), (6, 60.0)])
```

## Reading and aggregates

```python
from tsedge import Aggregate

points = db.read_range("air.temperature", 1, 4)
avg = db.aggregate("air.temperature", 1, 4, Aggregate.AVG)
windows = db.aggregate_windowed("air.temperature", 1, 1001, 100)
```

`read_range` returns copied `Point` objects. `aggregate_windowed` returns non-empty `WindowAggregate` buckets.

## Verification and CSV export

```python
report = db.verify()
db.export_csv("air.temperature", 1, 4, "temperature.csv")
```

To verify a database path without opening it first:

```python
from tsedge import verify_database

report = verify_database("sensor_db")
```

## Disk quota

```python
db.set_disk_quota(128 * 1024 * 1024)
db.enforce_disk_quota()
```

## Error handling

Most methods raise `TSEdgeError` when the native library returns a negative status code. The exception exposes both `code` and `message`.
