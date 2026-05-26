"""Type stubs for the loglite C++ extension module."""

from __future__ import annotations

class HarvesterDef:
    type: str
    name: str
    config: dict[str, str]

class Config:
    host: str
    port: int
    log_table_name: str
    harvesters: list[HarvesterDef]

    @staticmethod
    def from_file(path: str) -> Config: ...

def run_server(config_path: str) -> None:
    """Start the HTTP server.  Blocks until a shutdown signal is received."""
    ...

def stop_server() -> None:
    """Signal a running server to shut down from any thread."""
    ...

def rollout(config_path: str, start_version: int = -1) -> None:
    """Apply pending migrations."""
    ...

def rollback(config_path: str, version: int, force: bool = False) -> None:
    """Roll back a single migration."""
    ...

def push_to_backlog(log: dict) -> None:
    """Push a log entry dict into the active server backlog (thread-safe)."""
    ...
