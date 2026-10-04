# C++ test suite

Run the suite with `cpp/run-tests.sh` from the repository root. GTest filters select a contract, for example:

```bash
cpp/run-tests.sh --gtest_filter='DatabaseTest.*:MigrationManagerTest.*'
cpp/run-tests.sh --gtest_filter='ServerTest.*:ServerApiTest.*:AsyncDatabaseTest.*'
```

## Where each contract is tested

| Layer | Tests | Focus |
| --- | --- | --- |
| Parsing and configuration | `test_utils`, `test_query_filters`, `test_schema`, `test_config` | Named case tables for aliases, malformed input, numeric boundaries, and environment precedence. |
| Memory and ownership | `test_backlog`, `test_column_dict`, `test_log_notifier`, `test_metrics` | FIFO/capacity, failure restoration, dictionary identity, cancellation, retention, and balanced lifetimes. |
| SQLite and maintenance | `test_database`, `test_migration_manager`, `test_diagnostics`, `test_vacuum` | Exact rows/fields, ordering, rollback, commit failure, locked/partial reads, and actual page reclamation. |
| Handlers | `test_handlers` | Request validation, response data, projections, pagination, and metrics without opening a listener. |
| Transport and execution | `test_server`, `test_async_database`, `test_read_pool` | Routing, encoding, keep-alive, SSE, executor affinity, serialization, and concurrent reads/writes. |
| Lifecycle and files | `test_api`, `test_file_harvester` | Public API cleanup, shutdown persistence, fatal errors, partial lines, truncation, and rotation. |

## Keeping tests useful and compact

- Add a case at the narrowest layer that can expose the bug. Socket tests should verify transport or lifecycle behavior; handler tests own exhaustive request validation.
- Group variations of one contract into a case table with `SCOPED_TRACE`. Keep distinct failure mechanisms separate: an insert failure, a commit failure, a lock, and a failure after the first row require different regressions.
- Assert observable consequences: exact rows and values, retained batch order, cancellation error codes, or a responsive executor. A call followed by `SUCCEED()` or a successful insert without reading it back adds little protection.
- Use `test_support.hpp` for owned temporary directories, the shared SQLite fixture/schema, and bounded polling. Preserve test-specific schemas where the migration itself is under test.
- Wait for readiness rather than sleeping to guess when a server has started. Use bounded synchronized work for concurrency tests. File polling tests retain waits where the production poll cycle itself is being exercised.

Concurrent count and row queries currently use separate SQLite snapshots. The read-pool test checks monotonic totals and row limits while inserts run, then checks the exact final total after writers finish; it does not promise snapshot consistency.
