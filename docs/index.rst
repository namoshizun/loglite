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

Example:

.. code-block:: yaml

   host: 0.0.0.0
   port: 7788
   debug: true
   auto_rollout: false
   log_table_name: Log
   log_timestamp_field: timestamp
   sqlite_dir: ./db
   allow_origin: "*"
   sse_limit: 1000
   sse_debounce_ms: 500
   vacuum_max_days: 7
   vacuum_max_size: 500MB
   vacuum_target_size: 400MB

   sqlite_params:
     auto_vacuum: INCREMENTAL
     journal_mode: WAL
     synchronous: NORMAL

   migrations:
     - version: 1
       rollout:
         - |
           CREATE TABLE Log (
             id INTEGER PRIMARY KEY AUTOINCREMENT,
             timestamp DATETIME NOT NULL,
             message TEXT NOT NULL,
             level TEXT NOT NULL,
             service TEXT NOT NULL,
             extra JSON
           );
       rollback:
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

   .. code-block:: bash

      curl "http://localhost:7788/logs?fields=message,timestamp&limit=10&offset=0&timestamp=>=2023-04-01T00:00:00,<=2023-04-01T05:00:00&level==WARNING"

   Special query parameters:

   - ``fields``: comma-separated fields to return (default ``*``)
   - ``limit``: max rows
   - ``offset``: result offset

``GET /logs/sse``
   Subscribe to real-time updates via SSE.

   .. code-block:: bash

      curl -H "Accept: text/event-stream" http://localhost:7788/logs/sse?fields=message,timestamp

Harvesters
----------

Built-in harvesters:

- ``FileHarvester``: tail local files and ingest JSON log lines
- ``ZMQHarvester``: receive JSON logs from ZeroMQ (PULL/SUB)
- ``SocketHarvester``: listen on TCP/Unix sockets for JSON lines

Custom harvester support:

- Inherit from ``loglite.harvesters.base.Harvester``
- Place your class on ``PYTHONPATH``
- Reference it in config via ``harvesters[].type``

License
-------

`MIT <https://github.com/namoshizun/loglite/blob/main/LICENSE>`_
