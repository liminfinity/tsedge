# Disk Quota

TSEdge supports a soft runtime disk quota for database files.

Quota cleanup removes old completed segment files when it is safe to do so. It does not rewrite compressed blocks or delete individual points inside a block.

## Cleanup rules

- The quota is soft, not a hard reservation.
- Old sealed segment files are cleanup candidates.
- The active segment is not removed.
- The last segment of a series is not removed.
- WAL, metadata and arbitrary non-segment files are not used as cleanup targets.
- If safe cleanup is impossible and the database is still above the limit, TSEdge returns a quota exceeded error.

## Python example

```python
from tsedge import TSEdge

with TSEdge.open("sensor_db") as db:
    db.set_disk_quota(128 * 1024 * 1024)
    db.enforce_disk_quota()
```

`set_disk_quota(0)` disables the quota.
