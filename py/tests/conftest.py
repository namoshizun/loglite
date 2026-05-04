"""
Install a lightweight stub for the loglite._core C++ extension module when it
has not been compiled.  This lets the Python-only test suite run without
building the full C++ project.

When running against a real build, the stub is never installed because
``import loglite._core`` succeeds before this code runs.
"""

import sys
import types
from dataclasses import dataclass, field
from unittest.mock import MagicMock

import pytest


@dataclass
class _HarvesterDef:
    type: str = ""
    name: str = ""
    config: dict = field(default_factory=dict)


@dataclass
class _Config:
    host: str = "127.0.0.1"
    port: int = 7788
    log_table_name: str = "Log"
    harvesters: list = field(default_factory=list)

    @staticmethod
    def from_file(path: str) -> "_Config":
        return _Config()


def _make_stub() -> types.ModuleType:
    stub = types.ModuleType("loglite._core")
    stub.HarvesterDef = _HarvesterDef  # type: ignore[attr-defined]
    stub.Config = _Config  # type: ignore[attr-defined]
    stub.run_server = MagicMock()  # type: ignore[attr-defined]
    stub.stop_server = MagicMock()  # type: ignore[attr-defined]
    stub.rollout = MagicMock()  # type: ignore[attr-defined]
    stub.rollback = MagicMock()  # type: ignore[attr-defined]
    stub.push_to_backlog = MagicMock()  # type: ignore[attr-defined]
    return stub


try:
    import loglite._core  # noqa: F401  real extension present — nothing to do
except ImportError:
    stub = _make_stub()
    sys.modules["loglite._core"] = stub
    # Also make `from loglite import _core` work by attaching to the package.
    import loglite

    loglite._core = stub  # type: ignore[attr-defined]


# ── Shared fixtures ────────────────────────────────────────────────────────────


@pytest.fixture()
def captured_logs(monkeypatch: pytest.MonkeyPatch) -> list[dict]:
    """Capture all calls to _core.push_to_backlog; return the collected list."""
    from loglite import _core

    logs: list[dict] = []
    monkeypatch.setattr(_core, "push_to_backlog", lambda entry: logs.append(entry))
    return logs
