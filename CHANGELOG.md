## Changelog

### 0.2.1

- refactor: data vacuuming now supports  `auto_vacuum: INCREMENTAL` mode and removes freeable pages in batches. Switch to full vacuuming if configured the FULL vacuuming mode.

### 0.2.0

- feat: supports column based compression, adds `compression` section in config file to specify which columns are compressible. The user should make sure those columns are integer type.
- feat: creates `column_dictionary` table to store the mapping between unique column values and their canonical ids. Load the dictionary into memory during initialization.

### 0.1.8

- refactor: delete expired logs in batches (default size: 2500) when vacuuming

### 0.1.7

- feat: trigger log entries clean-up based on `vacuum_max_days` and `vacuum_max_size` configurations.
- feat: read config from environment variables using prefix "LOGLITE_", this can override the config file settings.

### 0.1.6

- fix: pass the current event loop to asyncio.Event for p39

### 0.1.5

- feat: ingestion view pushes logs to the backlog, which gets periodically flushed to the database using bulk insert (default interval: 2 seconds)

### 0.1.4

- fix: initialize db before migration
- fix: should always set request.validated_data, sse view catches connection reset

### 0.1.3

- feat: show query/ingestion stats every `task_diagnostics_interval` seconds
- docs: demonstrate creating loguru handler to push log messages to loglite server

### 0.1.2

- feat: allow specify multiple query filters per field, improve modularization
- feat: log stream subscrition via server-sent-event, support debouncing

### 0.1.1

- Allow settings SQLite parameters in config file.

### 0.1.0

- Initial release.
