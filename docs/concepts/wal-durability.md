# WAL and Durability

The write-ahead log (WAL) is the recovery bridge between an append call and a compressed segment block.

When an append is accepted, TSEdge records it in the WAL before relying on the in-memory buffer. If the process crashes before the buffer is flushed to a segment, the next `tsedge_open` can replay complete WAL records.

## Recovery behavior

- Complete WAL records are replayed on database open.
- An incomplete final WAL record is treated as a torn write and ignored.
- A complete record with a bad checksum is treated as corruption.
- WAL recovery is local to the embedded database directory.

TSEdge WAL is not a full transactional database engine. It restores accepted records but does not provide multi-series transactional atomicity.

## Durability modes

### FAST

FAST favors throughput. WAL data may be buffered before reaching `wal.log`, so a process or system crash can lose the most recent buffered appends.

### BALANCED

BALANCED flushes WAL data more often than FAST while avoiding the cost of syncing every append.

### STRICT

STRICT flushes WAL data on every append or batch append. It provides the strongest crash recovery behavior available in TSEdge.

## Flush and close

`tsedge_flush`, `tsedge_flush_all` and `tsedge_close` flush pending data through the normal WAL and segment path before completing. For most applications, closing the database cleanly is enough to persist the current buffers into segment files.
