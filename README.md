<div align="center">
  <img src="docs/loglite.png" alt="LogLite Logo" width="170"/>

### A lightweight, high-performance logging service with SQLite and async RESTful API.
</div>

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

## Configuration

LogLite requires a YAML configuration file. Here's a sample configuration:

```yaml
# Web API server bind host
host: 0.0.0.0
# Web API server bind port
port: 7788
# More verbose logging when enabled
debug: true
# Whether to automatically apply pending migrations during server startup (default: false)
auto_rollout: false
# Name of the main log entry table in SQLite, **required**
log_table_name: Log
# Name of the column storing the log timestamp, used for removing logs older than N days (default: timestamp)
log_timestamp_field: timestamp
# Directory for SQLite database
sqlite_dir: ./db
# CORS configuration (default: *)
allow_origin: "*"
# Maximum number of logs to push in a single SSE event payload (default: 1000)
sse_limit: 1000
# Debounce time in milliseconds for SSE, logs may be pushed later if they arrive too quickly (default: 500)
sse_debounce_ms: 500
# Remove logs older than this number of days (default: 3650 days)
vacuum_max_days: 7
# Remove the oldest logs when the db size exceeds this value (default: 1TB)
vacuum_max_size: 500MB
# When above triggered, remove the oldest logs until the db size is blow this value (default: 800GB)
vacuum_target_size: 400MB

# Optional: see the "Harvesters" for details
harvesters:
  - type: loglite.harvesters.FileHarvester
    name: app logs
    config:
      path: /tmp/cool-stuff/app.log

# You can set any SQLite parameters, no default values
sqlite_params:
  auto_vacuum: INCREMENTAL
  journal_mode: WAL
  synchronous: NORMAL
  cache_size: -32000  # 32MB
  foreign_keys: OFF
  temp_store: MEMORY
  mmap_size: 52428800  # 50MB

# Database migrations, **required**
migrations:
  - version: 1  # Incremental migration version
    rollout:  # Raw SQLite statements
      - |
        CREATE TABLE Log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME NOT NULL,
            message TEXT NOT NULL,
            level TEXT NOT NULL CHECK (level IN ('DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL')),
            service TEXT NOT NULL,
            filename TEXT,
            path TEXT,
            line INTEGER,
            function TEXT,
            pid INTEGER,
            process_name TEXT,
            extra JSON
        );
      - CREATE INDEX IF NOT EXISTS idx_timestamp ON Log(timestamp);
      - CREATE INDEX IF NOT EXISTS idx_level ON Log(level);
      - CREATE INDEX IF NOT EXISTS idx_service ON Log(service);
    rollback:  # SQL statements to apply when rolling back
      - DROP INDEX IF EXISTS idx_service;
      - DROP INDEX IF EXISTS idx_level;
      - DROP INDEX IF EXISTS idx_timestamp;
      - DROP TABLE IF EXISTS Log;
```

### Required Configuration Items

- **log_table_name**: Name of the main log entry table in SQLite.
- **migrations**: At least one migration must be defined with version, rollout and rollback statements
  - **version**: A unique integer for each migration
  - **rollout**: SQL statements to apply when migrating forward
  - **rollback**: SQL statements to apply when rolling back

Other items are optional with sensible defaults.

## Usage

### Running Migrations

Before starting the server, you may need to apply migrations to set up the database schema. You can skip this setup if `auto_rollout` is set to  `true`.

```bash
loglite migrate rollout -c /path/to/config.yaml
```

### Starting the Server

To start the LogLite server:

```bash
loglite server run -c /path/to/config.yaml
```

### Rolling Back Migrations

If you need to roll back a specific migration version (e.g., version id = 3):

```bash
loglite migrate rollback -c /path/to/config.yaml -v 3
```

Add the `-f` flag to force rollback without confirmation.

## HTTP Endpoints

### POST /logs

Insert a new log entry. The payload format must be consistent with your log table schema.

```bash
curl -X POST http://localhost:7788/logs \
  -H "Content-Type: application/json" \
  -d '{
    "timestamp": "2023-04-01T12:34:56",
    "message": "This is a test log message",
    "level": "INFO",
    "service": "my-service",
    "extra": {"request_id": "12345"}
  }'
```

### GET /logs

Query logs with filters. Each query parameter specifies a **field** and its **filters**. A filter defines the operator (e.g. =, ~=, !=, >=, <=, >, <) and the value. Filters are comma-separated.

Example request:
```bash
curl "http://localhost:7788/logs?fields=message,timestamp&limit=10&offset=0&timestamp=>=2023-04-01T00:00:00,<=2023-04-01T05:00:00&level==WARNING"
```

Example response:

```json
{
    "status": "success",
    "result": {
        "total": 3,
        "offset": 0,
        "limit": 2,
        "results": [
            {
                "timestamp": "2025-04-01T02:44:04.207515",
                "message": "hello world!"
            },
            {
                "timestamp": "2025-04-01T01:44:04.207515",
                "message": "hello world!"
            }
        ]
    }
}
```

The following are special query parameters, just provide the exact value:

- ``fields``: Comma-separated list of fields to return, defaults to "*" (select all fields).
- ``limit``: Maximum number of logs to return.
- ``offset``: Offset in the result set.


### GET /logs/sse

Subscribe to real-time log updates via Server-Sent Events (SSE). Query parameters only accept ``fields``, not filtering is applied.

```bash
curl -H "Accept: text/event-stream" http://localhost:7788/logs/sse?fields=message,timestamp
```

Example events:

```
data: [{"timestamp": "2025-04-01T02:44:04.207515", "message": "first msg"}]
data: [{"timestamp": "2025-04-01T02:44:10.207515", "message": "third msg"}, {"timestamp": "2025-04-01T01:44:05.207515", "message": "second msg"}]
```

## Harvesters

LogLite comes with built-in harvesters to collect logs from various sources.

### Built-in Harvesters

#### FileHarvester

Tails a local file and ingests new lines as they are written. It handles log rotation (by tracking inode) and truncation. Each log line must be a valid JSON conforming to your log table schema.

```yaml
harvesters:
  - type: loglite.harvesters.FileHarvester
    name: app-logs
    config:
      path: /var/log/app.log
```

#### ZMQHarvester

Receives JSON logs via a ZeroMQ socket. Supports both PULL and SUB sockets, and can either bind or connect.

```yaml
harvesters:
  - type: loglite.harvesters.ZMQHarvester
    name: zmq-logs
    config:
      endpoint: tcp://127.0.0.1:5555
      socket_type: PULL  # or SUB
      bind: true         # or false to connect
```

#### SocketHarvester

Starts a TCP or Unix domain socket server and listens for newline-delimited JSON logs.

```yaml
harvesters:
  - type: loglite.harvesters.SocketHarvester
    name: tcp-logs
    config:
      host: 0.0.0.0
      port: 9000
```

### Custom Harvesters

Implement your own harvester by inheriting from `loglite.harvesters.base.Harvester`.

**Example: `my_harvester.py`**

```python
import asyncio
from dataclasses import dataclass
from typing import Type
from datetime import datetime
from loglite.harvesters.base import Harvester, BaseHarvesterConfig

@dataclass
class MyHarvesterConfig(BaseHarvesterConfig):
    interval: int = 1

class MyHarvester(Harvester[MyHarvesterConfig]):

    async def run(self):
        # Access config via self.config
        interval = self.config.interval

        while self._running:
            # Call this to push logs to LogLite
            await self.ingest({
              "timestamp": datetime.utcnow().isoformat(),
              "message": "hello from the outside 🎶",
              "level": "INFO",
              "service": "custom-harvester",
              "extra": {"request_id": "12345"}
            })

            # **Remember not to block the event loop**
            await asyncio.sleep(interval)
```

**Configuration:**

Make sure `my_harvester.py` is in your `PYTHONPATH`, then configure it in `config.yaml`:

```yaml
harvesters:
  - type: my_project.logger.MyHarvester
    name: custom-harvester
    config:
      interval: 5
```

## License

[MIT](LICENSE)
