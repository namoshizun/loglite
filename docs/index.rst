:hide-toc:

.. image:: loglite.png
   :alt: LogLite logo
   :width: 170
   :align: center

A lightweight, high-performance logging service with SQLite and an async REST API.

- SQLite backend for efficient log storage and querying
- Fully customizable table schema
- Built-in migration utilities
- REST API for ingestion and query
- Server-Sent Events (SSE) for real-time notifications
- **Native `C++ implementation <https://github.com/namoshizun/loglite/tree/main/cpp>`_ available as a drop-in replacement** 🚀

Installation
------------

Supports Python 3.10+.

.. code-block:: bash

   pip install loglite


Configuration
-------------

LogLite uses a YAML configuration file.

Required items:

- ``log_table_name``: name of the main SQLite log table
- ``migrations``: migration list with:

  - ``version``: unique integer
  - ``rollout``: SQL statements to apply
  - ``rollback``: SQL statements to revert

Most other configuration values are optional and have sensible defaults.

Following is a basic example. See `enable-compression.yaml <https://github.com/namoshizun/loglite/blob/main/configs/enable-compression.yaml>`_ for how to enable enumeration compression.

.. code-block:: yaml

   # Bind host
   host: 0.0.0.0
   # Bind port
   port: 7788
   # Verbose logging
   debug: true
   # Apply pending migrations on startup (default: false)
   auto_rollout: false
   # Main log entry table name in SQLite, **required**
   log_table_name: Log
   # Timestamp column name, used for vacuum by age (default: timestamp)
   log_timestamp_field: timestamp
   # SQLite database directory
   sqlite_dir: ./db
   # CORS allowed origin (default: *)
   allow_origin: "*"
   # Max logs per SSE event payload (default: 1000)
   sse_limit: 1000
   # SSE debounce in ms; logs are batched if they arrive faster than this (default: 500)
   sse_debounce_ms: 500
   # Drop logs older than N days (default: 3650)
   vacuum_max_days: 7
   # Trigger vacuum when db exceeds this size (default: 1TB)
   vacuum_max_size: 500MB
   # After vacuum triggered, remove oldest logs until db is below this size (default: 800GB)
   vacuum_target_size: 400MB

   # Optional: see the "Harvesters" for details
   harvesters:
   - type: loglite.harvesters.FileHarvester
      name: app logs
      config:
         path: /tmp/cool-stuff/app.log

   # Any valid SQLite PRAGMA key/value pairs
   sqlite_params:
      # Loglite natively supports incremental vacuuming, useful for controlling
      # the vacuuming overhead.
      auto_vacuum: INCREMENTAL
      journal_mode: WAL
      synchronous: NORMAL
      cache_size: -32000  # 32MB
      foreign_keys: OFF
      temp_store: MEMORY
      mmap_size: 52428800  # 50MB

   # Database migrations, **required**
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
               filename TEXT,
               path TEXT,
               line INTEGER,
               function TEXT,
               pid INTEGER,
               process_name TEXT,
               extra JSON
         );
         - CREATE INDEX IF NOT EXISTS idx_timestamp ON Log(timestamp);
         - CREATE INDEX IF NOT EXISTS idx_level ON Log(level) WHERE level IN ('WARNING', 'ERROR', 'CRITICAL');
         - CREATE INDEX IF NOT EXISTS idx_service ON Log(service);
      rollback:
         - DROP INDEX IF EXISTS idx_service;
         - DROP INDEX IF EXISTS idx_level;
         - DROP INDEX IF EXISTS idx_timestamp;
         - DROP TABLE IF EXISTS Log;


Usage
-----

Run migrations:

.. code-block:: bash

   loglite migrate rollout -c /path/to/config.yaml

Start the server:

.. code-block:: bash

   loglite server run -c /path/to/config.yaml

Rollback a migration version:

.. code-block:: bash

   loglite migrate rollback -c /path/to/config.yaml -v 3

Use ``-f`` to force rollback without confirmation.

HTTP Endpoints
--------------

``POST /logs``
   Insert a new log entry.

   .. code-block:: bash

      curl -X POST http://localhost:7788/logs \
        -H "Content-Type: application/json" \
        -d '{
          "timestamp": "2023-04-01T12:34:56",
          "message": "This is a test log message",
          "level": "INFO",
          "service": "my-service"
        }'

``GET /logs``
   Query logs with filters.

   Reserved parameters (all required):

   - ``fields``: comma-separated fields to return, or ``*`` for all
   - ``limit``: max rows to return
   - ``offset``: pagination offset

   Any other query parameter is interpreted as a field filter. Its value is one or more ``<operator><value>`` expressions; comma-separate them to AND multiple conditions on the same field.

   Supported operators: ``=``, ``!=``, ``>``, ``>=``, ``<``, ``<=``, ``~=`` (substring match)

   .. code-block:: bash

      # Filter by exact level
      curl "http://localhost:7788/logs?fields=*&limit=100&offset=0&level==ERROR"

      # Timestamp range (two conditions on the same field, comma-separated)
      curl "http://localhost:7788/logs?fields=message,timestamp&limit=50&offset=0&timestamp=>=2024-01-01T00:00:00,<=2024-01-02T00:00:00"

      # Substring match on message
      curl "http://localhost:7788/logs?fields=*&limit=100&offset=0&message=~=timeout"

   **In production, only filter on indexed fields**. Unindexed filters cause a full table scan. Define indices in your migration for every field you intend to query frequently.

``GET /logs/sse``
   Subscribe to real-time updates via SSE.

   .. code-block:: bash

      curl -H "Accept: text/event-stream" http://localhost:7788/logs/sse?fields=message,timestamp

Harvesters
----------

LogLite comes with built-in harvesters to collect logs from various sources. Each harvested log entry must be a JSON object in the same format as ``POST /logs``. See `file-harvester.yaml <https://github.com/namoshizun/loglite/blob/main/configs/file-harvester.yaml>`_ for a full example.


FileHarvester
~~~~~~~~~~~~~

Tails a local file and ingests each line as a JSON log entry.

.. code-block:: yaml

   harvesters:
   - type: loglite.harvesters.FileHarvester
      name: app-logs
      config:
         path: /var/log/app.log


ZMQHarvester
~~~~~~~~~~~~~

Receives JSON logs over a ZeroMQ PULL or SUB socket.

.. code-block:: yaml

   harvesters:
   - type: loglite.harvesters.ZMQHarvester
      name: zmq-logs
      config:
         endpoint: tcp://127.0.0.1:5555
         socket_type: PULL  # or SUB
         bind: true         # true to bind, false to connect

SocketHarvester
~~~~~~~~~~~~~~~~

Listens on a TCP or Unix socket for newline-delimited JSON logs.

.. code-block:: yaml

   harvesters:
   - type: loglite.harvesters.SocketHarvester
      name: tcp-logs
      config:
         host: 0.0.0.0
         port: 9000

Custom Harvesters
~~~~~~~~~~~~~~~~~~

Implement your own harvester by subclassing ``loglite.harvesters.base.Harvester``:

.. code-block:: python

   import asyncio
   from dataclasses import dataclass
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
               await self.ingest({
                   "timestamp": datetime.utcnow().isoformat(),
                   "message": "hello from the outside 🎶",
                   "level": "INFO",
                   "service": "custom-harvester",
                   "extra": {"request_id": "12345"}
               })

               # Do not block the event loop
               await asyncio.sleep(interval)

Make sure ``my_harvester.py`` is on your ``PYTHONPATH``, then reference it in ``config.yaml``:

.. code-block:: yaml

   harvesters:
   - type: my_project.logger.MyHarvester  # Import path to your harvester class
      name: custom-harvester
      config:  # Same fields as defined in `MyHarvesterConfig`
         interval: 5


License
-------

`MIT <https://github.com/namoshizun/loglite/blob/main/LICENSE>`_
