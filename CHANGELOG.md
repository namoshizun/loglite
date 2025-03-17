## Changelog

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
