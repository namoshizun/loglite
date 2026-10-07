"""Type stubs for the loglite C++ extension module."""

from __future__ import annotations

from collections.abc import Callable

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

def run_server(
    config_path: str,
    on_ready: Callable[[], None] | None = None,
    on_stop: Callable[[], None] | None = None,
) -> None:
    """Start the HTTP server. Blocks until shutdown.

    ``on_ready`` runs after the schema is usable. ``on_stop`` only signals
    producers; call ``notify_producers_finished`` after they have stopped.
    """
    ...

def stop_server() -> None:
    """Signal a running server to shut down from any thread."""
    ...

def current_epoch() -> int:
    """Epoch of the active server run, or 0 when none is running."""
    ...

def notify_producers_finished(epoch: int) -> None:
    """Tell that run its external producers have stopped."""
    ...

class Submission:
    """Handle bound to one server run."""

    def bound(self) -> bool: ...
    def push(self, log: dict) -> None: ...

def capture_submission() -> Submission:
    """Handle for the active run. A closed handle does not enter a later run."""
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
