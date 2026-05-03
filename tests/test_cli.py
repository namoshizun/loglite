from __future__ import annotations

import asyncio
import signal
from typing import Any

import pytest

from loglite import cli


class FakeConfig:
    migrations = [{"version": 1, "rollout": ["up"], "rollback": ["down"]}]


class FakeConfigLoader:
    paths: list[str] = []

    @classmethod
    def from_file(cls, config_path: str) -> FakeConfig:
        cls.paths.append(config_path)
        return FakeConfig()


class FakeDatabase:
    initialized = 0
    entered = 0
    exited = 0

    def __init__(self, config: FakeConfig):
        self.config = config

    async def __aenter__(self):
        FakeDatabase.entered += 1
        return self

    async def __aexit__(self, exc_type: Any, exc: Any, tb: Any):
        FakeDatabase.exited += 1

    async def initialize(self):
        FakeDatabase.initialized += 1


class FakeMigrationManager:
    applied: list[int] = []
    rolled_back: list[tuple[int, bool]] = []

    def __init__(self, db: FakeDatabase, migrations: list[dict[str, Any]]):
        self.db = db
        self.migrations = migrations

    async def apply_pending_migrations(self, start_version: int = -1):
        self.applied.append(start_version)

    async def rollback_migration(self, version_id: int, force: bool = False):
        self.rolled_back.append((version_id, force))


class FakeRunner:
    cleaned = False

    async def cleanup(self):
        self.cleaned = True


class FakeServer:
    instances: list["FakeServer"] = []

    def __init__(self, db: FakeDatabase, config: FakeConfig):
        self.db = db
        self.config = config
        self.runner = FakeRunner()
        self.setup_called = False
        self.started = False
        self.instances.append(self)

    async def setup(self):
        self.setup_called = True

    async def start(self):
        self.started = True
        return self.runner, object()


@pytest.fixture(autouse=True)
def patch_cli_runtime(monkeypatch: pytest.MonkeyPatch):
    FakeConfigLoader.paths = []
    FakeDatabase.initialized = 0
    FakeDatabase.entered = 0
    FakeDatabase.exited = 0
    FakeMigrationManager.applied = []
    FakeMigrationManager.rolled_back = []
    FakeServer.instances = []

    monkeypatch.setattr(cli, "Config", FakeConfigLoader)
    monkeypatch.setattr(cli, "Database", FakeDatabase)
    monkeypatch.setattr(cli, "MigrationManager", FakeMigrationManager)
    monkeypatch.setattr(cli, "LogLiteServer", FakeServer)


@pytest.mark.asyncio
async def test_rollout_initializes_database_and_applies_migrations():
    await cli._rollout("loglite.yml", start_version=3)

    assert FakeConfigLoader.paths == ["loglite.yml"]
    assert FakeDatabase.entered == 1
    assert FakeDatabase.initialized == 1
    assert FakeDatabase.exited == 1
    assert FakeMigrationManager.applied == [3]


@pytest.mark.asyncio
async def test_rollback_initializes_database_and_rolls_back_migration():
    await cli._rollback("loglite.yml", version_id=2, force=True)

    assert FakeConfigLoader.paths == ["loglite.yml"]
    assert FakeDatabase.initialized == 1
    assert FakeMigrationManager.rolled_back == [(2, True)]


@pytest.mark.asyncio
async def test_run_server_sets_up_server_and_cleans_up(monkeypatch: pytest.MonkeyPatch):
    async def stop_after_start(seconds: float):
        raise asyncio.CancelledError

    class Loop:
        handlers: list[signal.Signals] = []

        def add_signal_handler(self, sig: signal.Signals, callback):
            self.handlers.append(sig)

    loop = Loop()
    monkeypatch.setattr(cli.asyncio, "get_event_loop", lambda: loop)
    monkeypatch.setattr(cli.asyncio, "sleep", stop_after_start)

    await cli._run_server("loglite.yml")

    server = FakeServer.instances[0]
    assert server.setup_called is True
    assert server.started is True
    assert server.runner.cleaned is True
    assert loop.handlers == [signal.SIGHUP, signal.SIGTERM, signal.SIGINT]


@pytest.mark.asyncio
async def test_shutdown_cancels_pending_tasks(monkeypatch: pytest.MonkeyPatch):
    cancelled = False

    async def never_finishes():
        nonlocal cancelled
        try:
            await asyncio.Event().wait()
        except asyncio.CancelledError:
            cancelled = True
            raise

    class Loop:
        stopped = False

        def stop(self):
            self.stopped = True

    task = asyncio.create_task(never_finishes())
    await asyncio.sleep(0)
    loop = Loop()
    current_task = asyncio.current_task()
    monkeypatch.setattr(cli.asyncio, "current_task", lambda: current_task)
    monkeypatch.setattr(cli.asyncio, "all_tasks", lambda: {current_task, task})

    await cli._shutdown(signal.SIGTERM, loop)  # pyright: ignore[reportArgumentType]

    assert task.cancelled()
    assert cancelled is True
    assert loop.stopped is True


def test_cli_commands_delegate_to_async_entrypoints(monkeypatch: pytest.MonkeyPatch):
    calls = []

    def run_coroutine(coro):
        calls.append(coro)
        coro.close()

    monkeypatch.setattr(cli.asyncio, "run", run_coroutine)

    cli.run("server.yml")
    cli.rollout("migrations.yml", version_id=5)
    cli.rollback("migrations.yml", version_id=4, force=True)

    assert [coro.cr_code.co_name for coro in calls] == ["_run_server", "_rollout", "_rollback"]
