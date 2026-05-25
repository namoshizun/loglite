:hide-toc:

.. image:: logo.svg
   :alt: LogLite logo
   :width: 270
   :align: center
   :class: loglite-doc-hero-logo

A lightweight, high-performance logging service — SQLite-backed, with a REST + SSE API,
implemented in modern C++ and installable as ``pip install loglite`` ✨✨.

LogLite is **one process on one host** that ingests logs two ways:

- **Push** — clients ``POST /logs`` over the network.
- **Pull** — built-in harvesters actively pull from external sources (file, socket, ZeroMQ).

You get **structured, indexable, live-tailable** logs and an **web dashboard** (optional & standalone) 
without standing up a database server or an observability cluster.


When to use
-----------

**LogLite is built for**

- One device, many services - replace a pile of log files with one indexed & queryable store.
- Edge & appliances — gateway, robot, or box that collects logs from nearby services or devices over HTTP.
- Low ops, low RAM — no JVM, single process, low memory footprint; backup is copying a `.sqlite` file.

**LogLite is NOT built for**

Multi-node aggregation, sharding, or enterprise SIEM. If you need Loki, Elastic, Splunk, or ClickHouse-scale search
across a fleet, use those tools. LogLite does not federate peers or isolate tenants.


Installation
------------

**Python package**

Pre-built wheels are published to PyPI for Python 3.10+. The wheel bundles the compiled C++ core, so no separate build step is needed

.. code-block:: bash

   pip install loglite

   # Also enable the ZeroMQ harvester
   pip install "loglite[zmq]"

**Standalone C++ binary**

For build-from-source instructions and Docker images for Linux release builds, see `cpp/README.md <https://github.com/namoshizun/loglite/blob/main/cpp/README.md>`_. Recommended for memory-constrained environments. The musl-built binary is a self-contained ~5MB program.

Configuration
-------------

LogLite is configured with a single YAML file. Two fields are required —
``log_table_name`` and ``migrations`` — everything else has a sensible default.

A minimal example:

.. code-block:: yaml

    log_table_name: Log
    sqlite_params:  # Any valid SQLite PRAGMA key/value pairs
       auto_vacuum: INCREMENTAL
       journal_mode: WAL  # Highly recommended
       synchronous: NORMAL
       busy_timeout: 5000
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

A full annotated example, including vacuuming, SSE, harvesters, and SQLite pragmas:

.. code-block:: yaml

   # ── Server ───────────────────────────────────────────────
   host: 0.0.0.0      # Bind host
   port: 7788         # Bind port
   debug: true        # Verbose logging
   allow_origin: "*"  # CORS Access-Control-Allow-Origin
   db_pool_size: 2    # Read DB pool. Positive int, or "auto" (= hardware concurrency);
                      # More readers can help queries but use N times more RAM because
                      # each SQLite connection holds a distinct cache memory.

   # ── Database ─────────────────────────────────────────────
   sqlite_dir: ./db       # Directory holding the SQLite db file
   auto_rollout: false    # Apply pending migrations on startup

   sqlite_params:         # Any valid SQLite PRAGMA key/value pairs
     auto_vacuum: INCREMENTAL   # Required to use incremental vacuuming
     journal_mode: WAL
     synchronous: NORMAL
     cache_size: -32000   # 32 MB
     temp_store: MEMORY
     mmap_size: 52428800  # 50 MB

   # ── Log table ────────────────────────────────────────────
   log_table_name: Log
   log_timestamp_field: timestamp   # Column used for age-based vacuum

   # ── SSE ──────────────────────────────────────────────────
   sse_limit: 1000          # Max logs per SSE event payload
   sse_debounce_ms: 500     # Coalesce bursts faster than this window

   # ── Vacuum ───────────────────────────────────────────────
   vacuum_max_days: 7         # Drop logs older than N days
   vacuum_max_size: 500MB     # Trigger vacuum when db exceeds this
   vacuum_target_size: 400MB  # Trim oldest rows until db is under this

   # ── Background tasks ──────────────────────────────────────
   task_diagnostics_interval: 60   # Seconds between stats collections
   task_backlog_flush_interval: 5  # Seconds between backlog flush passes
   task_backlog_max_size: 200      # Max backlog entries before force-flush
   task_vacuum_interval: 120       # Seconds between incremental vacuum pass
   task_vacuum_max_size: 20        # MB budget per incremental vacuum pass
   stats_retention_hours: 24       # Hours to keep stats data before pruning

   # ── Optional: column compression ─────────────────────────
   # See configs/enable-compression.yaml for the full example.
   # Listed columns must be declared INTEGER in the schema; LogLite
   # transparently maps unique values to canonical ids.
   compression:
     enabled: true
     columns: [service, filename, path, function, process_name]

   # ── Optional: harvesters ─────────────────────────────────
   harvesters:
     - type: loglite.harvesters.FileHarvester
       name: app-logs
       config:
         path: /var/log/app.log

   # ── Migrations (required) ────────────────────────────────
   migrations:
     - version: 1
       rollout:    [...]   # SQL statements to apply
       rollback:   [...]   # SQL statements to revert

See `configs/ <https://github.com/namoshizun/loglite/tree/main/configs>`_ in
the repo for runnable examples (``basic.yaml``, ``enable-compression.yaml``,
``file-harvester.yaml``).


Command Line Interface
----------------------

Three subcommands, all driven by the same config file.

.. code-block:: bash

   # Apply pending migrations
   loglite migrate rollout -c /path/to/config.yaml

   # Roll back a specific migration version (use -f to skip the prompt)
   loglite migrate rollback -c /path/to/config.yaml -v 3

   # Start the server (blocks; SIGINT / SIGTERM shut down cleanly)
   loglite server run -c /path/to/config.yaml


HTTP API
--------

``POST /logs``
~~~~~~~~~~~~~~

Insert a single log entry, or a JSON array of entries for batch ingestion.
The body is appended to the in-memory backlog and bulk-inserted in the
background; the response returns immediately with ``{"status": "accepted"}``.

.. code-block:: bash

   curl -X POST http://localhost:7788/logs \
     -H "Content-Type: application/json" \
     -d '{
       "timestamp": "2026-05-05T12:34:56.123",
       "message": "User signed in",
       "level": "INFO",
       "service": "auth"
     }'


``GET /logs``
~~~~~~~~~~~~~

Query stored logs with field filters and pagination.

Reserved parameters (all required):

- ``fields`` — comma-separated columns to return, or ``*`` for all
- ``limit`` — maximum rows
- ``offset`` — pagination offset

Any other query parameter is interpreted as a **filter** on a column. Its value
is one or more ``<operator><value>`` expressions; comma-separate them to AND
multiple conditions on the same field.

Supported operators: ``=``, ``!=``, ``>``, ``>=``, ``<``, ``<=``, ``~=``
(substring match).

.. code-block:: bash

   # Exact match
   curl "http://localhost:7788/logs?fields=*&limit=100&offset=0&level==ERROR"

   # Time range (two conditions on the same field)
   curl "http://localhost:7788/logs?fields=message,timestamp&limit=50&offset=0\
   &timestamp=>=2026-01-01T00:00:00,<=2026-01-02T00:00:00"

   # Substring match on message
   curl "http://localhost:7788/logs?fields=*&limit=100&offset=0&message=~=timeout"

.. note::

   Filters always translate to a ``WHERE`` on the SQLite table. **In production,
   only filter on indexed columns** — define indices in your migration for every
   field you intend to query frequently, otherwise expect full table scans.


``GET /logs/sse``
~~~~~~~~~~~~~~~~~

Subscribe to new logs in real time over `Server-Sent Events
<https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events>`_. The
``fields`` parameter behaves the same as on ``GET /logs``. Bursts of writes are
coalesced according to ``sse_debounce_ms``.

.. code-block:: bash

   curl -N --output - -H "Accept: text/event-stream" \
     "http://localhost:7788/logs/sse?fields=message,timestamp,level"


``GET /stats``
~~~~~~~~~~~~~~

Returns runtime performance statistics sampled at regular intervals (controlled by ``task_diagnostics_interval``).
Each sample captures request rates, latencies, throughput, and live
connection counts.


**Query parameters** (all required except ``ordering``):

- ``since``, ``until`` — ISO-8601 time window (closed-open), e.g., ``2026-05-01T00:00:00Z,2026-05-01T01:00:00Z``. **Must be ≤ 1 day apart.**
- ``activity_stats_fields`` — comma-separated columns to return, or ``*`` for all
- ``database_stats_fields`` — comma-separated columns to return, or ``*`` for all
- ``ordering`` — ``asc`` or ``desc`` (default: ``desc``)

**Activity stats columns** (interval measurements):

- ``since``, ``until`` — start and end of the interval (ISO‑8601, closed‑open).
- *Log query fields*

  - ``query_count`` — number of ``GET /logs`` requests received.
  - ``query_min`` — min query response time (ms).
  - ``query_max`` — max query response time (ms).
  - ``query_avg`` — average query response time (ms).

- *Log ingestion fields*

  - ``ingest_count`` — number of ``POST /logs`` requests received.
  - ``ingest_size_min`` — min request body size (bytes).
  - ``ingest_size_max`` — max request body size (bytes).
  - ``ingest_size_avg`` — average request body size (bytes).
  - ``ingest_drop_count`` — log entries discarded from the backlog due to buffer overflow.

- *Log insertion fields*

  - ``insert_batch_count`` — number of insert batches flushed to the database.
  - ``insert_total_count`` — total log entries inserted across all batches.
  - ``insert_total_cost`` — total time spent on database insertion (ms).

- *Live connections fields* (point-in-time gauge at end of interval)

  - ``sse_session_count`` — active SSE subscriptions.
  - ``http_conn_count`` — active HTTP connections (including SSE sessions).

**Database stats columns** (point-in-time snapshot):

- ``timestamp`` — sample time (ISO‑8601).
- ``rows_count`` — total log rows stored in the database.
- ``db_size`` — database file size (bytes).

**Example query:**

.. code-block:: bash

   curl "http://localhost:7788/stats?\
   since=2026-05-01T00:00:00Z&until=2026-05-01T01:00:00Z&\
   activity_stats_fields=id,since,until,query_count&database_stats_fields=*&ordering=desc"

Response:

.. code-block:: json

   {
     "activities": {
       "fields": ["id", "since", "until", "query_count"],
       "data": [
         [2, "2026-05-01T00:59:00Z", "2026-05-01T01:00:00Z", 120],
         [1, "2026-05-01T00:58:00Z", "2026-05-01T00:59:00Z", 95]
       ]
     },
     "database": {
       "fields": ["id", "timestamp", "rows_count", "db_size"],
       "data": [
         [2, "2026-05-01T01:00:00Z", 45230, 5242880],
         [1, "2026-05-01T00:59:00Z", 45110, 5111808]
       ]
     },
     "uptime": 3600
   }

``uptime`` is the number of seconds since this server process started (integer).


``GET /version``
~~~~~~~~~~~~~~~~

Returns the version string of the running LogLite instance.

Response:

.. code-block:: json

   {"version": "1.2.0"}


``GET /settings``
~~~~~~~~~~~~~~~~~

Returns the server configuration values from the loaded config file.

Response:

.. code-block:: json

   {
     "settings": [
       {
         "key": "log_table_name",
         "value": "Log",
         "description": "SQLite table name used to store log records."
       },
       {
         "key": "sqlite_params",
         "value": {
           "journal_mode": "WAL",
           "synchronous": "NORMAL"
         },
         "description": "SQLite PRAGMA key/value pairs applied when opening the database."
       },
       {
         "key": "compression_enabled",
         "value": false,
         "description": "Whether dictionary compression is enabled for configured log columns."
       },
       {
         "key": "harvester_types",
         "value": ["loglite.harvesters.FileHarvester"],
         "description": "Harvester implementation types configured for this instance."
       }
     ]
   }

Included settings:

- ``log_table_name``, ``log_timestamp_field``
- ``sqlite_params`` (object of PRAGMA key/value pairs)
- ``auto_rollout``
- ``vacuum_max_days``, ``vacuum_max_size``, ``vacuum_target_size``, ``vacuum_delete_batch_size``
- ``task_diagnostics_interval``, ``task_backlog_flush_interval``, ``task_backlog_max_size``
- ``task_vacuum_interval``, ``task_vacuum_max_size``, ``stats_retention_hours``
- ``compression_enabled`` (boolean)
- ``harvester_types`` (array of harvester ``type`` strings from the config)


``GET /schema``
~~~~~~~~~~~~~~~

Returns the schema of the configured log table (column names and types).

Response:

.. code-block:: json

   {
     "table": "Log",
     "columns": [
       {
         "name": "timestamp",
         "kind": "datetime",
         "sqlite_type": "DATETIME",
         "compressed": false,
         "not_null": true,
         "primary_key": false
       },
       {
         "name": "service",
         "kind": "text",
         "sqlite_type": "INTEGER",
         "compressed": true,
         "not_null": true,
         "primary_key": false
       }
     ]
   }

Each column object includes:

- ``name`` — column name
- ``sqlite_type`` — declarative type from SQLite ``PRAGMA table_info``
- ``kind`` — one of ``integer, number, text, datetime, json, blob, boolean``
  ``text``, ``datetime``, ``json``, ``blob``, or ``boolean``
- ``compressed`` — when ``true``, the column is stored as integers but filtered
  using canonical string values (dictionary compression); ``kind`` is always
  ``text`` for compressed columns
- ``not_null``, ``primary_key`` — from the table definition


Harvesters
----------

A *harvester* is a long-running ingestion source that pushes entries into the
LogLite backlog without going through HTTP. They are configured in YAML under
``harvesters``. Each harvested entry must be a JSON object with the same shape
you would ``POST`` to ``/logs``.

Built-in harvesters
~~~~~~~~~~~~~~~~~~~

**FileHarvester** *(C++ core, always available)* — tails a file like ``tail -F``,
detects rotation and truncation, and parses each line as a JSON log entry.

.. code-block:: yaml

   harvesters:
     - type: loglite.harvesters.FileHarvester
       name: app-logs
       config:
         path: /var/log/app.log

**SocketHarvester** *(Python)* — listens on a TCP or Unix socket for
newline-delimited JSON entries.

.. code-block:: yaml

   harvesters:
     - type: loglite.harvesters.SocketHarvester
       name: tcp-logs
       config:
         host: 0.0.0.0
         port: 9000
         # or, for a Unix socket:
         # path: /tmp/loglite.sock

**ZMQHarvester** *(Python, requires* ``pip install "loglite[zmq]"`` *)* —
receives JSON entries from a ZeroMQ ``PULL`` or ``SUB`` socket.

.. code-block:: yaml

   harvesters:
     - type: loglite.harvesters.ZMQHarvester
       name: zmq-logs
       config:
         endpoint: tcp://127.0.0.1:5555
         socket_type: PULL   # or SUB
         bind: true          # true to bind, false to connect


Custom Python harvesters
~~~~~~~~~~~~~~~~~~~~~~~~

Subclass ``loglite.harvesters.Harvester[T]`` with a dataclass config, and call
``self.ingest(...)`` to push into the C++ backlog. Custom harvesters run on the
same event loop as the built-in Python ones.

.. code-block:: python

   import asyncio
   from dataclasses import dataclass
   from datetime import datetime, timezone

   from loglite.harvesters.base import BaseHarvesterConfig, Harvester


   @dataclass
   class HeartbeatConfig(BaseHarvesterConfig):
       interval: int = 5


   class HeartbeatHarvester(Harvester[HeartbeatConfig]):
       async def run(self) -> None:
           while self._running:
               self.ingest({
                   "timestamp": datetime.now(timezone.utc).isoformat(),
                   "message": "heartbeat",
                   "level": "INFO",
                   "service": "heartbeat",
               })
               await asyncio.sleep(self.config.interval)

Make sure the module is importable (i.e. on ``PYTHONPATH``), then reference it
in your config:

.. code-block:: yaml

   harvesters:
     - type: my_project.harvesters.HeartbeatHarvester
       name: heartbeat
       config:                 # Same fields as HeartbeatConfig
         interval: 30

Architecture
------------

LogLite has two pieces:

- **C++ core** — the server itself. HTTP + SSE (Boost.Asio + Beast), SQLite
  read/write, migrations, vacuuming, the in-memory backlog, the native file
  harvester, and column compression. Distributed as a single statically-linked
  binary, also embedded into the Python wheel via pybind11.

- **Python package (**``pip install loglite``**)** — a thin convenience layer:
  the ``loglite`` CLI, a ``Harvester[T]`` plugin base class, and a few built-in
  harvesters (``FileHarvester``, ``SocketHarvester``, ``ZMQHarvester``).
  Custom Python harvesters call ``self.ingest(log)``, which pushes the entry
  directly into the C++ backlog **in the same process** — no extra socket
  hop. The Python package adds zero server-side overhead.

If you don't need custom Python harvesters, you can also run the standalone
C++ binary directly. The config file and database are identical in both modes;
switching is a binary swap.

.. _runtime-memory-rss:

Runtime memory (RSS)
--------------------

LogLite is a single process with **bounded** in-memory structures (ingest
backlog, metrics sampling window, query/SSE result batches). RSS can still
**rise over minutes or hours** under load without indicating a memory leak.

**Why RSS increases**

- **SQLite page cache** — ``cache_size`` (for example ``-32000`` → 32 MiB) applies
  **per database connection**. The server opens one **writer** plus one **reader
  per hardware thread** (same count as the HTTP thread pool). Caches grow lazily
  toward those caps as the database is used.
- **Memory-mapped I/O** — ``mmap_size`` caps how much of the DB file each
  connection may map; mapping ramps up as the file grows (vacuum limits on-disk
  size separately).
- **Warm-up** — After startup or a traffic spike, RSS often climbs until caches
  and the on-disk database reach a steady size under your vacuum settings.

**When to investigate**

- Sustained growth **after** ``logs.db`` size has stabilized.
- Rising ``ingest_drop_count`` in activity stats (backlog cannot keep up).
- RSS far above what your ``sqlite_params`` and core count imply, with no
  heavy query/SSE load.

Tune ``cache_size`` and ``mmap_size`` downward on memory-constrained hosts; use
``task_backlog_max_size`` and ingest rate to keep the backlog small.



License
-------

`MIT <https://github.com/namoshizun/loglite/blob/main/LICENSE>`_
