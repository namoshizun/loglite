# LogLite C++ architecture

One owner for each policy. Callers cross a boundary with a value, not with access to the thing that enforces the policy.

## Owners

| Component                           | Owns                                                              |
| ----------------------------------- | ----------------------------------------------------------------- |
| `Runtime`                           | Lifetimes, executors, readiness, producer registration, shutdown  |
| `Ingestion`                         | Normalization, validation, admission, batching, settlement, retry |
| `QueryService`                      | Field resolution, operators, timestamp bounds, pagination         |
| `LogStore` / `LogReader`            | Partitions, ids, metadata, migrations, retention, file leases     |
| `WriterDatabase` / `ReaderDatabase` | Synchronous SQL for one file                                      |
| `LogNotifier`                       | Committed-record window and subscriber wakeups                    |
| HTTP and harvesters                 | Decoding, encoding, sockets, file mechanics                       |
| Metrics                             | Bounded observation windows                                       |

## Invariants

- Readiness requires valid settings and a usable log schema.
- Every source is prepared by `Ingestion` before it can reach the backlog.
- `accepted` means the entry was admitted to the volatile queue.
- An admitted entry is committed, rejected, evicted, or still explicitly pending.
- A missing field is absent from the prepared object. An explicit JSON null stays null.
- Partition routing consumes the instant `Ingestion` resolved. It does not invent timestamps.
- A committed entry is removed from the queue before live-feed publication.
- Publication failure does not change that outcome.
- Plain and compressed reads use the same logical comparison.
- Pagination totals are exact counts. `EstimateLogRowCount` is a separate API.
- Shutdown lets registered producers finish, seals admission, settles accepted work, then destroys executors and connections.
