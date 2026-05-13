:hide-toc:

.. image:: loglite.png
   :alt: LogLite logo
   :width: 270
   :align: center
   :class: loglite-doc-hero-logo

A lightweight, high-performance logging service — SQLite-backed, with a REST + SSE API,
implemented in modern C++ and installable as ``pip install loglite`` ✨✨.

LogLite is **one process on one host** that ingests logs two ways:

- **Push** — clients ``POST /logs`` over the network.
- **Pull** — built-in harvesters actively pull from external sources (file, socket, ZeroMQ).

You get **structured, indexable, live-tailable** logs without standing up a database server
or an observability cluster.


When to use
-----------

LogLite is a good fit when:

- **You want a small, central log endpoint** for a fleet of edge devices, microservices,
  or jobs to ``POST`` to. Great for IoT gateways, on-prem appliances, robots, home-lab
  boxes, dev/CI environments.
- **You're consolidating co-located services on one device.** Instead of every program
  writing its own file — awkward to filter, storage-heavy for structured records, no
  live view — pipe them all into LogLite and let SQLite indexes plus optional column
  compression do the work.
- **You like SQLite's "the database is a file" model**: trivial to back up, copy, and
  inspect with any sqlite client.
- **You want a low runtime cost** — low RAM, no JVM, no extra services to babysit.

LogLite is **not** a replacement for Elastic Stack, Loki, Splunk, or ClickHouse. A single
instance does not scale horizontally or federate with peers. If you need multi-node
aggregation, sharding, or tenant isolation, reach for one of those instead.

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
   host: 0.0.0.0          # Bind host
   port: 7788             # Bind port
   debug: true            # Verbose logging
   allow_origin: "*"      # CORS Access-Control-Allow-Origin

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
       "timestamp": "2026-05-05T12:34:56Z",
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


``GET /stats``
~~~~~~~~~~~~~~

Query the internal runtime performance statistics collected by the diagnostics
background task. The diagnostics task snapshots per-interval metrics (query
latency, ingest throughput, backlog drops, insert batches, live connection
gauges) and persists them to the ``activity_stats`` and ``database_stats``
internal tables.

Reserved parameters (all required except ``ordering``):

- ``since``, ``until`` — ISO-8601 time window. **Must be ≤ 1 day apart.**
- ``activity_stats_fields`` — comma-separated columns to return, or ``*`` for all
- ``database_stats_fields`` — comma-separated columns to return, or ``*`` for all
- ``ordering`` — ``asc`` or ``desc`` (default: ``desc``)

Known ``activity_stats`` columns: ``since``, ``until``, ``query_count``,
``query_min``, ``query_max``, ``query_avg``, ``ingest_count``,
``ingest_size_min``, ``ingest_size_max``, ``ingest_size_avg``,
``ingest_drop_count``, ``insert_batch_count``, ``insert_total_count``,
``insert_total_cost``, ``sse_session_count``, ``http_conn_count``.

Known ``database_stats`` columns: ``timestamp``, ``rows_count``, ``db_size``.

.. code-block:: bash

   curl "http://localhost:7788/stats?\
   since=2026-05-01T00:00:00Z&until=2026-05-01T01:00:00Z&\
   activity_stats_fields=*&database_stats_fields=*&ordering=desc"

Response:

.. code-block:: json

   {
     "activities": {
       "fields": ["since", "until", "query_count", "query_min", ...],
       "data": [
         ["2026-05-01T00:59:00Z", "2026-05-01T01:00:00Z", 120, 1, 250, ...],
         ["2026-05-01T00:58:00Z", "2026-05-01T00:59:00Z", 95, 1, 180, ...]
       ]
     },
     "database": {
       "fields": ["timestamp", "rows_count", "db_size"],
       "data": [
         ["2026-05-01T01:00:00Z", 45230, 5242880],
         ["2026-05-01T00:59:00Z", 45110, 5111808]
       ]
     }
   }

Each stats table returns data in a columnar format: ``fields`` lists the
column names in order, and ``data`` is a list of rows where each row is an
array of values matching the field order.


``GET /logs/sse``
~~~~~~~~~~~~~~~~~

Subscribe to new logs in real time over `Server-Sent Events
<https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events>`_. The
``fields`` parameter behaves the same as on ``GET /logs``. Bursts of writes are
coalesced according to ``sse_debounce_ms``.

.. code-block:: bash

   curl -N -H "Accept: text/event-stream" \
     "http://localhost:7788/logs/sse?fields=message,timestamp,level"


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


Roadmap
-------

- Built-in web UI for browsing logs and database stats
- Time-based partitioning (one SQLite file per day or month)


License
-------

`MIT <https://github.com/namoshizun/loglite/blob/main/LICENSE>`_
