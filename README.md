<div align="center">
  <img src="docs/logo.svg" alt="LogLite Logo" width="170"/>

### One box. One process. Structured logs in SQLite — query over HTTP, tail over SSE.
</div>

<p float="left">
  <img src="https://github.com/namoshizun/loglite/actions/workflows/deploy-docs.yml/badge.svg?branch=main" />
  <img src="https://github.com/namoshizun/loglite/actions/workflows/python-tests.yml/badge.svg?branch=main&event=push" />
  <img src="https://github.com/namoshizun/loglite/actions/workflows/cpp-tests.yml/badge.svg?branch=main&event=push" />
  <img src="https://img.shields.io/codecov/c/github/namoshizun/loglite/main?flag=python&logo=python" alt="python coverage" />
  <img src="https://img.shields.io/codecov/c/github/namoshizun/loglite/main?flag=cpp&logo=cplusplus" alt="cpp coverage" />
  <img src="https://img.shields.io/pypi/v/loglite" alt="python version" />
</p>

**LogLite** is a small logging *service* for a **single machine**: ingest with `POST /logs` or built-in harvesters (files, sockets, ZeroMQ), store in **your SQLite schema**, search and tail without standing up Elasticsearch or a DB server. Ships as `pip install loglite` (C++ core inside the wheel) or a standalone binary (~5 MB musl build).

## Built for

- **Edge & appliances** — gateway, robot, or box that collects logs from nearby services or devices over HTTP.
- **One device, many writers** — replace a pile of log files with one indexed store, live tail, and optional web UI.
- **Low ops, low RAM** — no JVM, no log cluster; backup is copying a `.sqlite` file.

## Not built for

**Multi-node aggregation, sharding, or enterprise SIEM.** If you need Loki, Elastic, Splunk, or ClickHouse-scale search across a fleet, use those tools. LogLite does not federate peers or isolate tenants.

## What you get

|            |                                                                                                     |
| ---------- | --------------------------------------------------------------------------------------------------- |
| **Ingest** | REST bulk backlog · file/socket/ZMQ harvesters · Python `Harvester` plugins                         |
| **Store**  | SQLite + WAL · migrations you write · retention vacuum · optional enum column compression           |
| **Use**    | Filtered `GET /logs` · `GET /logs/sse` live tail · optional [dashboard](frontend/README.md) (v1.2+) |

Core is **C++20** (Asio/Beast); Python is a thin CLI and harvester layer. Same `config.yaml` for the wheel or the [standalone binary](cpp/README.md).

## Install

```bash
pip install loglite          # Python 3.10+
pip install "loglite[zmq]"   # To enable the ZeroMQ harvester
```

Or grab the standalone C++ binary — see [`cpp/README.md`](cpp/README.md)

## Quick start

`config.yaml` (minimal):

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
  -d '{"timestamp":"2026-05-05T12:00:00.123Z","message":"hello","level":"INFO","service":"demo"}'
```

Tail in real time:

```bash
curl -N --output - -H "Accept: text/event-stream" "http://localhost:7788/logs/sse?fields=message,timestamp,level"
```

## Web Dashboard

Since **v1.2.0**, LogLite includes an optional **web dashboard** (see [`frontend/README.md`](frontend/README.md)): live tail, log search, stats, and settings. Use it for **observability and debugging** —— insights into service performance, availability, and storage. The docker image is published per release.

## Documentation

Full configuration reference, HTTP API, harvester plugin guide, and recipes:
**[loglite.lu-d.com](https://loglite.lu-d.com/)**

## Roadmap

- [x] Bulk insert with backlog
- [x] Column-based compression for enum-like fields
- [x] Harvester plugin system (file / socket / ZMQ / custom)
- [x] Native C++ core
- [x] `/stats` endpoint for DB and background-task metrics
- [x] Built-in web UI for browsing logs
- [ ] Time-based partitioning (one SQLite file per day or month)

## License

[MIT](LICENSE)
