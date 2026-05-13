<div align="center">
  <img src="docs/loglite.png" alt="LogLite Logo" width="170"/>

### A lightweight, high-performance logging service. SQLite-backed, with a REST + SSE API.
</div>

<p float="left">
  <img src="https://github.com/namoshizun/loglite/actions/workflows/deploy-docs.yml/badge.svg?branch=main" />
  <img src="https://github.com/namoshizun/loglite/actions/workflows/python-tests.yml/badge.svg?branch=main&event=push" />
  <img src="https://github.com/namoshizun/loglite/actions/workflows/cpp-tests.yml/badge.svg?branch=main&event=push" />
  <img src="https://img.shields.io/codecov/c/github/namoshizun/loglite/main?flag=python&logo=python" alt="python coverage" />
  <img src="https://img.shields.io/codecov/c/github/namoshizun/loglite/main?flag=cpp&logo=cplusplus" alt="cpp coverage" />
  <img src="https://img.shields.io/pypi/v/loglite" alt="python version" />
</p>

LogLite is **one process on one host** that ingests logs two ways:

- **Push** — clients `POST /logs` over the network.
- **Pull** — built-in harvesters actively pull from external sources (file, socket, ZeroMQ).

It speaks plain HTTP, stores logs in SQLite, and ships as a single C++ binary (or `pip install` away).

## When to use LogLite

LogLite is a good fit when:

- **You want a small, central log endpoint** for a fleet of edge devices, microservices, or jobs to `POST` to. Ideal for IoT gateways, on-prem appliances, robots, home-lab boxes, dev/CI environments.
- **You're consolidating co-located services on one box.** Instead of every program writing its own file — awkward to filter, storage-heavy for structured records, no live view — pipe them all into LogLite and let SQLite indexes plus optional column compression do the work.
- **You like SQLite's "the database is a file" model**: trivial to back up, copy, and inspect with any sqlite client.
- **You want a small runtime footprint** — low RAM, no JVM, no extra services to babysit.

LogLite is **not** a replacement for Elastic Stack, Loki, Splunk, or ClickHouse. A single instance does not scale horizontally or federate with peers. If you need multi-node aggregation, sharding, or tenant isolation, reach for one of those instead.

## Highlights

- **Single binary core.** The server is implemented in modern C++20 with Boost.Asio + Beast. No interpreter, no JVM, no daemon zoo.
- **SQLite storage with full schema control.** You write the migrations; LogLite makes no assumptions about your log table structure.
- **REST + Server-Sent Events.** `POST /logs` to ingest, `GET /logs` to query with rich filter operators, `GET /logs/sse` to tail in real time.
- **Built-in retention.** Time- and size-based vacuuming with incremental SQLite vacuum to keep IO predictable.
- **Bulk ingest.** Entries are buffered in a backlog and bulk-inserted on a timer, so a single `POST` does not equal a single `INSERT`.
- **Optional column compression.** Dictionary-encode high-cardinality enum columns (`service`, `path`, ...) into integer ids to shrink storage and speed up filters.
- **Plugin-friendly Python layer.** `pip install loglite` gives you the CLI, the C++ server, and a simple `Harvester` base class for writing custom log ingestors in Python.

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│  pip install loglite                                       │
│                                                            │
│   ┌───────────────────────┐    ┌─────────────────────┐     │
│   │ Python plugin layer   │    │ Built-in harvesters │     │
│   │  - CLI (typer)        │───▶│  Socket, ZMQ, ...   │     │
│   │  - Harvester[T] API   │    │  + your own         │     │
│   └──────────┬────────────┘    └──────────┬──────────┘     │
│              │ pybind11 (_core)           │ ingest()       │
│              ▼                            ▼                │
│   ┌────────────────────────────────────────────────────┐   │
│   │              LogLite C++ core                      │   │
│   │  HTTP + SSE  ·  SQLite  ·  migrations  ·  vacuum   │   │
│   │  backlog · file harvester · column compression     │   │
│   └────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────┘
```

The Python package is a **thin convenience layer**. The HTTP server, SQLite I/O, migrations, SSE, and vacuuming all live in the C++ core. Custom Python harvesters push entries directly into the C++ backlog inside the same process — no socket round-trip, no extra network hop.

If you do not need custom Python harvesters, you can also use the **C++ binary directly** (see [`cpp/`](cpp/README.md)). The same config file and database work unchanged in both modes.

## Installation

```bash
pip install loglite          # Python 3.10+; pre-built wheels on PyPI
pip install "loglite[zmq]"   # also enable the ZeroMQ harvester
```

Or grab the standalone C++ binary — see [`cpp/README.md`](cpp/README.md) for build and Docker instructions.

## Quick start

Create `config.yaml`:

```yaml
host: 0.0.0.0
port: 7788
log_table_name: Log
sqlite_dir: ./db
auto_rollout: true

sqlite_params:
   # Any valid SQLite PRAGMA key/value pairs
   auto_vacuum: INCREMENTAL
   journal_mode: WAL  # Highly recommended
   synchronous: NORMAL

migrations:
  - version: 1
    rollout:
      - |
        CREATE TABLE Log (
            id INTEGER PRIMARY KEY,
            timestamp DATETIME NOT NULL,
            message TEXT NOT NULL,
            level TEXT NOT NULL CHECK (level IN ('DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL')),
            service TEXT NOT NULL,
            extra JSON
        );
      - CREATE INDEX IF NOT EXISTS idx_timestamp ON Log(timestamp);
      - CREATE INDEX IF NOT EXISTS idx_level ON Log(level) WHERE level IN ('WARNING', 'ERROR', 'CRITICAL');
    rollback:
      - DROP INDEX IF EXISTS idx_level;
      - DROP INDEX IF EXISTS idx_timestamp;
      - DROP TABLE IF EXISTS Log;
```

Run the server:

```bash
loglite server run -c config.yaml
```

Send a log:

```bash
curl -X POST http://localhost:7788/logs \
  -H "Content-Type: application/json" \
  -d '{"timestamp":"2026-05-05T12:00:00Z","message":"hello","level":"INFO","service":"demo"}'
```

Tail in real time:

```bash
curl -N -H "Accept: text/event-stream" "http://localhost:7788/logs/sse?fields=*"
```

## Documentation

Full configuration reference, HTTP API, harvester plugin guide, and recipes:
**[loglite.lu-d.com](https://loglite.lu-d.com/)**

## Roadmap

- [x] Bulk insert with backlog
- [x] Column-based compression for enum-like fields
- [x] Harvester plugin system (file / socket / ZMQ / custom)
- [x] Native C++ core
- [x] `/stats` endpoint for DB and background-task metrics
- [ ] Built-in web UI for browsing logs
- [ ] Time-based partitioning (one SQLite file per day or month)

## License

[MIT](LICENSE)
