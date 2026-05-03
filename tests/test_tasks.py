from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Any

import pytest

from loglite.backlog import Backlog
from loglite.globals import BACKLOG, INGESTION_STATS, LAST_INSERT_LOG_ID, QUERY_STATS
from loglite.tasks import diagnostics
from loglite.tasks.flush_backlog import _flush_backlog
from loglite.tasks.vacuum import (
    _incremental_vacuum,
    _remove_excessive_logs,
    _remove_stale_logs,
    register_database_vacuuming_task,
)
from loglite.types import QueryFilter


@dataclass
class FlushConfig:
    task_backlog_flush_interval: float = 0.001
    debug: bool = False


class FlushDatabase:
    def __init__(self, max_log_id: int = 0):
        self.max_log_id = max_log_id
        self.inserted: tuple[dict, ...] = tuple()

    async def insert(self, logs: tuple[dict, ...]) -> int:
        self.inserted = logs
        return len(logs)

    async def get_max_log_id(self) -> int:
        return self.max_log_id


class VacuumDatabase:
    def __init__(self):
        self.column_info = [{"name": "timestamp"}]
        self.deleted_filters: list[list[QueryFilter]] = []
        self.size_mb = 200.0
        self.min_log_id = 11
        self.max_log_id = 20
        self.min_timestamp = datetime.now() - timedelta(days=10)
        self.pragmas: dict[str, int] = {
            "auto_vacuum": 1,
            "freelist_count": 5,
            "page_size": 4096,
        }
        self.incremental_vacuum_pages: list[int] = []
        self.vacuumed = False
        self.checkpoints: list[str] = []

    async def get_min_timestamp(self) -> datetime:
        return self.min_timestamp

    async def delete(self, filters: list[QueryFilter]) -> int:
        self.deleted_filters.append(filters)
        return 3

    async def get_size_mb(self) -> float:
        return self.size_mb

    async def get_min_log_id(self) -> int:
        return self.min_log_id

    async def get_max_log_id(self) -> int:
        return self.max_log_id

    async def get_pragma(self, name: str) -> int:
        return self.pragmas[name]

    async def incremental_vacuum(self, page_count: int):
        self.incremental_vacuum_pages.append(page_count)
        self.pragmas["freelist_count"] = 0

    async def wal_checkpoint(self, mode: str = "TRUNCATE"):
        self.checkpoints.append(mode)

    async def vacuum(self):
        self.vacuumed = True


@dataclass
class VacuumConfig:
    task_diagnostics_interval: int = 60
    task_vacuum_interval: int = 60
    task_vacuum_max_size: int = 20
    vacuum_max_days: int = 7
    vacuum_max_size_bytes: int = 100 * 1024 * 1024
    vacuum_target_size_bytes: int = 50 * 1024 * 1024
    vacuum_delete_batch_size: int = 4
    log_timestamp_field: str = "timestamp"


@pytest.fixture(autouse=True)
def reset_task_globals():
    BACKLOG.set(Backlog(10))
    INGESTION_STATS.reset()
    QUERY_STATS.reset()
    yield
    BACKLOG.reset()
    INGESTION_STATS.reset()
    QUERY_STATS.reset()


@pytest.mark.asyncio
async def test_flush_backlog_inserts_logs_and_publishes_latest_id():
    await BACKLOG.instance().add([{"message": "one"}, {"message": "two"}])
    db = FlushDatabase(max_log_id=42)

    await _flush_backlog(db, FlushConfig())  # pyright: ignore[reportArgumentType]

    assert db.inserted == ({"message": "one"}, {"message": "two"})
    assert await LAST_INSERT_LOG_ID.get() == 42
    assert await BACKLOG.instance().flush() == tuple()


@pytest.mark.asyncio
async def test_flush_backlog_ignores_empty_backlog():
    db = FlushDatabase(max_log_id=42)

    await _flush_backlog(db, FlushConfig())  # pyright: ignore[reportArgumentType]

    assert db.inserted == tuple()


@pytest.mark.asyncio
async def test_diagnostics_task_reports_and_resets_stats(monkeypatch: pytest.MonkeyPatch):
    def repeat_once(**kwargs: Any):
        def decorator(func):
            async def wrapped():
                await func()

            return wrapped

        return decorator

    monkeypatch.setattr(diagnostics, "repeat_every", repeat_once)
    INGESTION_STATS.collect(count=2, cost_ms=5)
    QUERY_STATS.collect(count=1, cost_ms=3)

    await diagnostics.register_diagnostics_task(VacuumConfig())  # pyright: ignore[reportArgumentType]

    assert INGESTION_STATS.get_stats()["count"] == 0
    assert QUERY_STATS.get_stats()["count"] == 0


@pytest.mark.asyncio
async def test_remove_stale_logs_deletes_entries_older_than_retention():
    db = VacuumDatabase()

    removed = await _remove_stale_logs(db, max_age_days=7)  # pyright: ignore[reportArgumentType]

    assert removed == 3
    assert db.deleted_filters[0][0]["field"] == "timestamp"
    assert db.deleted_filters[0][0]["operator"] == "<="


@pytest.mark.asyncio
async def test_remove_stale_logs_skips_when_data_is_recent():
    db = VacuumDatabase()
    db.min_timestamp = datetime.now()

    assert await _remove_stale_logs(db, max_age_days=7) == 0  # pyright: ignore[reportArgumentType]
    assert db.deleted_filters == []


@pytest.mark.asyncio
async def test_remove_excessive_logs_deletes_oldest_batches():
    db = VacuumDatabase()

    removed = await _remove_excessive_logs(
        db,
        max_size_mb=100,
        target_size_mb=50,
        batch_size=4,  # pyright: ignore[reportArgumentType]
    )

    assert removed == 6
    assert db.deleted_filters == [
        [{"field": "id", "operator": "<=", "value": 14}],
        [{"field": "id", "operator": "<=", "value": 17}],
    ]


@pytest.mark.asyncio
async def test_remove_excessive_logs_skips_when_database_is_small():
    db = VacuumDatabase()
    db.size_mb = 99

    assert await _remove_excessive_logs(db, 100, 50, 4) == 0  # pyright: ignore[reportArgumentType]
    assert db.deleted_filters == []


@pytest.mark.asyncio
async def test_incremental_vacuum_caps_free_pages():
    db = VacuumDatabase()
    db.pragmas["freelist_count"] = 10_000

    remaining = await _incremental_vacuum(db, max_size_mb=1)  # pyright: ignore[reportArgumentType]

    assert remaining == 0
    assert db.incremental_vacuum_pages == [256]


@pytest.mark.asyncio
async def test_incremental_vacuum_skips_without_free_pages():
    db = VacuumDatabase()
    db.pragmas["freelist_count"] = 0

    assert await _incremental_vacuum(db, max_size_mb=1) == 0  # pyright: ignore[reportArgumentType]
    assert db.incremental_vacuum_pages == []


@pytest.mark.asyncio
async def test_database_vacuum_task_runs_one_full_vacuum_cycle(monkeypatch: pytest.MonkeyPatch):
    db = VacuumDatabase()

    def repeat_once(**kwargs: Any):
        return lambda func: func

    monkeypatch.setattr("loglite.tasks.vacuum.repeat_every", repeat_once)

    await register_database_vacuuming_task(
        db,
        VacuumConfig(),  # pyright: ignore[reportArgumentType]
    )

    assert db.checkpoints == ["TRUNCATE", "FULL"]
    assert db.vacuumed is True
    assert db.deleted_filters
