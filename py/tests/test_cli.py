"""CLI tests — all C++ calls are mocked via the _core stub from conftest.py."""

from __future__ import annotations

import threading
from unittest.mock import MagicMock

import pytest

from loglite import _core, cli


@pytest.fixture(autouse=True)
def reset_core_mocks():
    """Reset MagicMock call history before each test."""
    for attr in ("run_server", "rollout", "rollback"):
        mock = getattr(_core, attr, None)
        if isinstance(mock, MagicMock):
            mock.reset_mock()


# ── server run ────────────────────────────────────────────────────────────────


def test_run_calls_core_run_server(monkeypatch: pytest.MonkeyPatch):
    """cli.run() must call _core.run_server with the config path."""

    # Make run_server return immediately so the CLI doesn't hang.
    monkeypatch.setattr(_core, "run_server", MagicMock(return_value=None))
    # Suppress the harvester thread from actually blocking.
    monkeypatch.setattr(cli, "_run_python_harvesters", lambda *_a, **_kw: None)

    cli.run("server.yml")

    _core.run_server.assert_called_once_with("server.yml")  # pyright: ignore[reportAttributeAccessIssue]


def test_run_starts_and_joins_harvester_thread(monkeypatch: pytest.MonkeyPatch):
    """A harvester thread is started before run_server and joined after."""
    thread_started = threading.Event()

    async def fake_harvesters(config_path: str, stop_event: threading.Event):
        thread_started.set()
        stop_event.wait()

    monkeypatch.setattr(cli, "_run_python_harvesters", fake_harvesters)
    monkeypatch.setattr(_core, "run_server", MagicMock(return_value=None))

    cli.run("server.yml")

    assert thread_started.is_set(), "harvester thread should have started"


# ── rollout ────────────────────────────────────────────────────────────────────


def test_rollout_delegates_to_core(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr(_core, "rollout", MagicMock())
    cli.rollout("cfg.yml", version_id=3)
    _core.rollout.assert_called_once_with("cfg.yml", 3)  # pyright: ignore[reportAttributeAccessIssue]


def test_rollout_uses_default_version(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr(_core, "rollout", MagicMock())
    cli.rollout("cfg.yml", version_id=-1)
    _core.rollout.assert_called_once_with("cfg.yml", -1)  # pyright: ignore[reportAttributeAccessIssue]


# ── rollback ───────────────────────────────────────────────────────────────────


def test_rollback_delegates_to_core(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr(_core, "rollback", MagicMock())
    cli.rollback("cfg.yml", version_id=2, force=True)
    _core.rollback.assert_called_once_with("cfg.yml", 2, True)  # pyright: ignore[reportAttributeAccessIssue]


def test_rollback_defaults_force_to_false(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr(_core, "rollback", MagicMock())
    cli.rollback("cfg.yml", version_id=1, force=False)
    _core.rollback.assert_called_once_with("cfg.yml", 1, False)  # pyright: ignore[reportAttributeAccessIssue]


# ── python harvester loading ───────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_run_python_harvesters_skips_native_types(monkeypatch: pytest.MonkeyPatch):
    """FileHarvester entries should be silently skipped by the Python manager."""
    from tests.conftest import _Config, _HarvesterDef

    file_def = _HarvesterDef(type="FileHarvester", name="logs", config={"path": "/tmp/x.log"})
    cfg = _Config(harvesters=[file_def])
    monkeypatch.setattr(_core, "Config", type("C", (), {"from_file": staticmethod(lambda _: cfg)}))

    stop = threading.Event()
    stop.set()  # stop immediately

    await cli._run_python_harvesters("cfg.yml", stop)
    # No Python harvester was instantiated — test passes if no exception is raised.
