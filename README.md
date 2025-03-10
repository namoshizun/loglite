# LogLite

A lightweight, high-performance logging service that stores log data in SQLite with HTTP APIs for log insertion and querying 📝.

## Features

- **Lightweight & Efficient**: Built with performance in mind using fully async libraries (aiohttp, aiofiles, orjson).
- **Fully customizable table schema**: Make no assumptions about the log table schema, just define your own schema.
- **SQLite Backend**: Store log messages in SQLite, enabling fast and complex queries.
- **Web API**: Insert and query logs via straightforward REST endpoints.
- **Database Migrations**: Built-in migration utilities to manage database schema changes.
- ✨✨✨ **More cool features in my wishlist**:
  - [ ] *Bulk insert*: Buffer log entries in memory for a short while or when a limit is reached, and bulk insert them into the database.
  - [ ] *Column based compression*: Mark some columns as "enums", silently create a "Enums" table which the main log table points to. Gradually grow the enums table to 
  captures all distinct values of that column.
  - [ ] *Time based partitioning*: One SQLite database per date or month.
  - [ ] *Just a logging handler*: Allow to be used as a basic logging handler without the Web API part.
  - [ ] *Log redirection*: When used as service, allow redirecting logs to local file or other external sink.
  - [ ] *CLI utilities*: More CLI utilities to directly query the database, and export the query results to a file.

## Installation

```bash
pip install loglite
```

## Configuration

LogLite requires a YAML configuration file. Here's a sample configuration:

```yaml
# Server configuration
host: 0.0.0.0  # Web API server bind host
port: 7788  # Web API server bind port
debug: true  # More verbose logging when enabled
log_table_name: Log  # Name of the main log entry table in SQLite
sqlite_dir: ./db  # Directory for SQLite database
allow_origin: "*"  # CORS configuration (default: *)
sqlite_params:  # you can set any SQLite parameters, no default values
  journal_mode: WAL
  synchronous: NORMAL
  cache_size: -32000  # 32MB
  foreign_keys: OFF
  temp_store: MEMORY
  mmap_size: 52428800  # 50MB

# Database migrations
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

- **migrations**: At least one migration must be defined with version, rollout and rollback statements
  - **version**: A unique integer for each migration
  - **rollout**: SQL statements to apply when migrating forward
  - **rollback**: SQL statements to apply when rolling back

Other items are optional with sensible defaults.

## Usage

### Running Migrations

Before starting the server, you need to apply migrations to set up the database schema:

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

## API Endpoints

### POST /logs

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

Query logs with filters. Each query parameter specifies the field, operation (=, ~=, !=, >=, <=, >, <) and value. The following are special query parameters that do not require operator, just provide the exact value:

- ``fields``: Comma-separated list of fields to return, defaults to "*" (select all fields).
- ``limit``: Maximum number of logs to return.
- ``offset``: Offset in the result set.


Example request:
```bash
curl "http://localhost:7788/logs?fields=message,timestamp&limit=10&offset=0&level=>INFO&service==backend&timestamp=>=2023-04-01T00:00:00"
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
                "timestamp": "2025-03-06T10:44:04.207515",
                "message": "hello world!"
            },
            {
                "timestamp": "2025-03-08T11:44:04.207515",
                "message": "hello world!"
            }
        ]
    }
}
```


## TODO:
- [x] Add basic documentation.
- [x] Customize SQLite configuration.
- [ ] Implement more features in the wishlist.
- [ ] Add tests.

## License

[MIT](LICENSE)