import pytest
import pytest_asyncio
from pathlib import Path
from datetime import datetime, timedelta
from typing import AsyncIterator

from loglite.config import Config
from loglite.database import Database
from loglite.types import CompressionConfig, Migration
from loglite.tasks.vacuum import (
    _remove_stale_logs,
    _remove_excessive_logs,
    _incremental_vacuum,
)


# --- Test Configuration ---
LOG_TABLE_NAME = "test_logs"

# Basic migration to create the test log table
MIGRATION_V1_UP = [
    f"""
    CREATE TABLE {LOG_TABLE_NAME} (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp TEXT NOT NULL,
        level TEXT,
        message TEXT,
        source TEXT,
        extra TEXT -- JSON stored as TEXT
    )
    """,
    f"CREATE INDEX idx_{LOG_TABLE_NAME}_timestamp ON {LOG_TABLE_NAME}(timestamp)",
    f"CREATE INDEX idx_{LOG_TABLE_NAME}_level ON {LOG_TABLE_NAME}(level)",
]
MIGRATION_V1_DOWN = [
    f"DROP INDEX idx_{LOG_TABLE_NAME}_level",
    f"DROP INDEX idx_{LOG_TABLE_NAME}_timestamp",
    f"DROP TABLE {LOG_TABLE_NAME}",
]


# --- Fixtures ---


@pytest.fixture
def base_config(tmp_path: Path) -> Config:
    return Config(
        sqlite_dir=tmp_path,
        log_table_name=LOG_TABLE_NAME,
        migrations=[
            Migration(
                version=1,
                rollout=MIGRATION_V1_UP,
                rollback=MIGRATION_V1_DOWN,
            ),
        ],
        compression=CompressionConfig(
            enabled=False,
            columns=[],
        ),
        sqlite_params={
            "journal_mode": "WAL",
            "synchronous": "NORMAL",
        },
    )


@pytest_asyncio.fixture
async def db_no_init(base_config: Config) -> AsyncIterator[Database]:
    db = Database(base_config)
    try:
        yield db
    finally:
        await db.close()


@pytest_asyncio.fixture
async def migrated_db(db_no_init: Database) -> AsyncIterator[Database]:
    await db_no_init.initialize()
    applied = await db_no_init.apply_migration(1, MIGRATION_V1_UP)
    assert applied is True
    yield db_no_init


# --- Test Cases ---


@pytest.mark.asyncio
async def test_date_based_expiration(migrated_db: Database):
    db = migrated_db

    # Insert 3 logs: 2 old, 1 fresh (use naive datetimes to match vacuum logic)
    now = datetime.now()
    logs = [
        {"timestamp": now - timedelta(days=3), "level": "INFO", "message": "old-1"},
        {"timestamp": now - timedelta(days=2), "level": "INFO", "message": "old-2"},
        {"timestamp": now, "level": "INFO", "message": "fresh"},
    ]
    n = await db.insert(logs)
    assert n == 3

    removed = await _remove_stale_logs(db, max_age_days=1)
    assert removed == 2

    res = await db.query(fields=("message",))
    assert res["total"] == 1
    assert res["results"][0]["message"] == "fresh"


@pytest.mark.asyncio
async def test_volume_based_expiration(migrated_db: Database, monkeypatch: pytest.MonkeyPatch):
    db = migrated_db

    # Insert 100 logs
    now = datetime.now()
    await db.insert(
        [
            {
                "timestamp": now - timedelta(seconds=i),
                "level": "INFO",
                "message": f"msg-{i}",
                "source": "vol",
            }
            for i in range(100)
        ]
    )

    # Pretend DB size is 100MB, enforce max 80MB with target 50MB => remove ~50%
    async def fake_size_mb():
        return 100.0

    monkeypatch.setattr(db, "get_size_mb", fake_size_mb)

    removed = await _remove_excessive_logs(db, max_size_mb=80.0, target_size_mb=50.0, batch_size=10)
    assert 45 <= removed <= 55  # approx half

    res = await db.query(fields=("id",))
    assert 45 <= res["total"] <= 55


@pytest.mark.asyncio
async def test_incremental_vacuum_reduces_freelist(tmp_path: Path):
    # Configure DB with INCREMENTAL auto_vacuum
    cfg = Config(
        sqlite_dir=tmp_path,
        log_table_name=LOG_TABLE_NAME,
        migrations=[
            Migration(version=1, rollout=MIGRATION_V1_UP, rollback=MIGRATION_V1_DOWN),
        ],
        compression=CompressionConfig(enabled=False, columns=[]),
        sqlite_params={
            "journal_mode": "WAL",
            "synchronous": "NORMAL",
            "auto_vacuum": "INCREMENTAL",
        },
    )
    db = Database(cfg)
    try:
        await db.initialize()
        applied = await db.apply_migration(1, MIGRATION_V1_UP)
        assert applied is True

        # Create freelist pages: insert then delete a bunch
        now = datetime.now()
        await db.insert(
            [
                {
                    "timestamp": now - timedelta(seconds=i),
                    "level": "INFO",
                    "message": f"inc-{i}",
                }
                for i in range(2000)
            ]
        )

        # Delete half to generate free pages
        await db.delete(
            [
                {"field": "id", "operator": "<=", "value": 1000},
            ]
        )

        before = await db.get_pragma("freelist_count")
        # In INCREMENTAL mode, deletes create freelist pages until vacuumed
        assert before is not None and before > 0

        remain = await _incremental_vacuum(db, max_size_mb=1024)  # large enough to clear all
        assert remain == 0

        after = await db.get_pragma("freelist_count")
        assert after == 0
    finally:
        await db.close()


@pytest.mark.asyncio
async def test_full_vacuum_reclaims_space(tmp_path: Path):
    # Configure DB with FULL auto_vacuum
    cfg = Config(
        sqlite_dir=tmp_path,
        log_table_name=LOG_TABLE_NAME,
        migrations=[
            Migration(version=1, rollout=MIGRATION_V1_UP, rollback=MIGRATION_V1_DOWN),
        ],
        compression=CompressionConfig(enabled=False, columns=[]),
        sqlite_params={
            "journal_mode": "WAL",
            "synchronous": "NORMAL",
            "auto_vacuum": "FULL",
        },
    )
    db = Database(cfg)
    try:
        await db.initialize()
        applied = await db.apply_migration(1, MIGRATION_V1_UP)
        assert applied is True

        # Insert rows and then delete to produce free pages
        now = datetime.now()
        await db.insert(
            [
                {
                    "timestamp": now - timedelta(milliseconds=i),
                    "level": "INFO",
                    "message": f"full-{i}",
                }
                for i in range(1500)
            ]
        )

        await db.delete(
            [
                {"field": "id", "operator": "<=", "value": 1000},
            ]
        )

        before = await db.get_pragma("freelist_count")
        # In FULL mode, free pages are reclaimed on commit
        assert before == 0

        # Full vacuum should reclaim free pages entirely
        await db.vacuum()
        await db.wal_checkpoint("FULL")

        after = await db.get_pragma("freelist_count")
        assert after == 0
    finally:
        await db.close()
