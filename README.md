<div align="center">
  <img src="docs/loglite.png" alt="LogLite Logo" width="170"/>

### A lightweight, high-performance logging service with SQLite and async RESTful API.
</div>

<p float="left">
  <img src="https://github.com/namoshizun/loglite/actions/workflows/deploy-docs.yml/badge.svg?branch=main" />
  <img src="https://github.com/namoshizun/loglite/actions/workflows/tests.yml/badge.svg?branch=main&event=push" />
  <a href="https://codecov.io/gh/namoshizun/loglite"><img src="https://codecov.io/gh/namoshizun/loglite/graph/badge.svg?branch=main" alt="codecov coverage" /></a>
</p>

**SQLite Backend** 💾 : Store log messages in SQLite, enabling efficient and complex queries.

**Fully customizable table schema** 🔧 : Make no assumptions about the log table structure, define your own schema to fit your needs.

**Database Migrations** 🔄 : Built-in migration utilities to manage database schema changes.

**Web API** 🌐 : RESTful endpoint for log ingestion and query. Support server-sent events (SSE) for real-time log notifications.

**Lightweight & Efficient** ⚡️ : Built with performance in mind:
  - Fully async libraries, with orjson for fast JSON serialization.
  - Supports incremental vacuuming to minimize IO / memory overhead.

**More cool features in my wishlist** ✨✨✨ :
  - [x] *Bulk insert*: Buffer log entries in memory for a short while or when a limit is reached, and bulk insert them into the database.
  - [x] *Column based compression*: Store the canonical ids for enum columns instead of the original values, saving disk space and improves query performance. See [enable-compression.yaml](configs/enable-compression.yaml) for an example configuration.
  - [x] *Harvester plugin system*: harvest logs from local files, ZeroMQ and TCP socket. Allow defining custom harvesters outside of the library.
  - [ ] *Time based partitioning*: One SQLite database per date or month.
  - [ ] *Just a logging handler*: Allow to be used as a basic logging handler without the Web API part.
  - [ ] *Log redirection*: When used as service, allow redirecting logs to local file or other external sink.
  - [ ] *CLI utilities*: More CLI utilities to directly query the database, and export the query results to a file.

## Installation

Supports Python 3.10+

```bash
pip install loglite
```

## Documentation

See [online doc](https://loglite.lu-d.com/) for more details.
