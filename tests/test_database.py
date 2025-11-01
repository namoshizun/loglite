import pytest
import pytest_asyncio
import orjson
from pathlib import Path
from datetime import datetime, timezone, timedelta
from typing import AsyncIterator

from loglite.config import Config
from loglite.database import Database
from loglite.types import CompressionConfig, QueryFilter, Migration


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
def test_config(tmp_path: Path) -> Config:
    """Provides a test Config object using a temporary path."""
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
async def db_no_init(test_config: Config) -> AsyncIterator[Database]:
    """Provides a Database instance without calling initialize."""
    db = Database(test_config)
    yield db
    await db.close()


@pytest_asyncio.fixture
async def initialized_db(db_no_init: Database) -> AsyncIterator[Database]:
    """Provides an initialized Database instance."""
    await db_no_init.initialize()
    yield db_no_init


@pytest_asyncio.fixture
async def migrated_db(initialized_db: Database) -> AsyncIterator[Database]:
    """Provides an initialized Database instance with the test log table created."""
    db = initialized_db
    applied = await db.apply_migration(1, MIGRATION_V1_UP)
    assert applied is True
    yield db


# --- Test Cases ---


@pytest.mark.asyncio
async def test_initialize(db_no_init: Database):
    """Test the initialize method creates required tables and initializes column_dict."""
    # The internal tables should not exist initially
    initial_conn = await db_no_init.get_connection()

    internal_tables = ["versions", "column_dictionary"]
    for table in internal_tables:
        cursor = await initial_conn.execute(
            f"SELECT name FROM sqlite_master WHERE type='table' AND name='{table}'"
        )
        assert await cursor.fetchone() is None

    await db_no_init.close()

    # After initialization, the internal tables should exist
    await db_no_init.initialize()
    assert db_no_init._connection is not None
    conn = await db_no_init.get_connection()  # Re-get potentially new connection
    for table in internal_tables:
        cursor = await conn.execute(
            f"SELECT name FROM sqlite_master WHERE type='table' AND name='{table}'"
        )
        row = await cursor.fetchone()
        assert row is not None and row[0] == table

    # Check column_dict is initialized to an empty dict
    assert hasattr(db_no_init, "_column_dict")
    assert db_no_init.column_dict.get_lookup() == dict()


@pytest.mark.asyncio
async def test_ping(initialized_db: Database):
    """Test the ping method returns True for an active connection."""
    assert await initialized_db.ping() is True


@pytest.mark.asyncio
async def test_close(initialized_db: Database):
    """Test the close method closes the connection and ping fails afterwards."""
    assert await initialized_db.ping() is True
    assert initialized_db._connection is not None

    await initialized_db.close()

    assert initialized_db._connection is None
    assert await initialized_db.ping() is True


@pytest.mark.asyncio
async def test_build_sql_query(migrated_db: Database):
    """Test the internal _build_sql_query method."""
    db = migrated_db

    # --- Test Cases ---
    test_cases = [
        # No filters
        {"filters": [], "expected_where": "1=1", "expected_params": []},
        # =
        {
            "filters": [{"field": "message", "operator": "=", "value": "test"}],
            "expected_where": "message = ?",
            "expected_params": ["test"],
        },
        # ~= LIKE
        {
            "filters": [{"field": "message", "operator": "~=", "value": "test"}],
            "expected_where": "message LIKE ?",
            "expected_params": ["%test%"],
        },
        # = and !=
        {
            "filters": [
                {"field": "message", "operator": "=", "value": "test"},
                {"field": "extra", "operator": "!=", "value": None},
            ],
            "expected_where": "message = ? AND extra != ?",
            "expected_params": ["test", None],
        },
    ]

    for case in test_cases:
        filters = case["filters"]
        where, params = db._build_sql_query(filters)
        assert where == case["expected_where"]
        assert params == case["expected_params"]


@pytest.mark.asyncio
async def test_insert_single(migrated_db: Database):
    """Test inserting a single log entry."""
    db = migrated_db
    log_entry = {
        "timestamp": (ts := datetime.now(timezone.utc)),
        "level": "INFO",
        "message": "Test log message",
        "source": "test_insert",
        "extra": {"key": "value"},
    }
    inserted_count = await db.insert(log_entry)
    assert inserted_count == 1

    # Verify insertion using query
    result = await db.query(fields=("*",), limit=1)
    assert result["total"] == 1
    assert len(result["results"]) == 1
    inserted_log = result["results"][0]
    assert inserted_log["level"] == "INFO"
    assert inserted_log["message"] == "Test log message"
    assert inserted_log["source"] == "test_insert"
    assert orjson.loads(inserted_log["extra"]) == {"key": "value"}

    assert abs(datetime.fromisoformat(inserted_log["timestamp"]) - ts) < timedelta(milliseconds=1)


@pytest.mark.asyncio
async def test_insert_multiple(migrated_db: Database):
    """Test inserting multiple log entries."""
    db = migrated_db
    ts1 = datetime.now(timezone.utc) - timedelta(minutes=1)
    ts2 = datetime.now(timezone.utc)
    log_entries = [
        {
            "timestamp": ts1,
            "level": "WARN",
            "message": "Warning message",
            "source": "test_insert_multi",
            "extra": None,
        },
        {
            "timestamp": ts2,
            "level": "ERROR",
            "message": "Error message",
            "source": "test_insert_multi",
            "extra": {"code": 123},
        },
    ]
    inserted_count = await db.insert(log_entries)
    assert inserted_count == 2

    # Verify insertion count
    result = await db.query(fields=("id",), limit=10)
    assert result["total"] == 2

    # Verify content and order (most recent first)
    result_full = await db.query(fields=("level", "message", "source"), limit=10)
    assert result_full["results"][0]["level"] == "ERROR"
    assert result_full["results"][0]["message"] == "Error message"
    assert result_full["results"][1]["level"] == "WARN"


@pytest.mark.asyncio
async def test_insert_missing_required(migrated_db: Database):
    """Test inserting a log missing a required field (timestamp)."""
    db = migrated_db
    log_entry = {
        # timestamp is NOT NULL
        "level": "INFO",
        "message": "Incomplete log",
    }
    # Should log a warning and insert 0 rows because validation happens before insert
    inserted_count = await db.insert(log_entry)
    assert inserted_count == 0

    # Verify no insertion occurred
    result = await db.query(fields=("id",))
    assert result["total"] == 0


@pytest.mark.asyncio
async def test_insert_invalid_batch(migrated_db: Database):
    """Test inserting a batch where some logs are valid and some are not."""
    db = migrated_db
    ts = datetime.now(timezone.utc)
    log_entries = [
        {  # Valid
            "timestamp": ts,
            "level": "INFO",
            "message": "Valid log 1",
            "source": "batch_test",
        },
        {  # Invalid (missing timestamp)
            "level": "WARN",
            "message": "Invalid log",
            "source": "batch_test",
        },
        {  # Valid
            "timestamp": ts,
            "level": "DEBUG",
            "message": "Valid log 2",
            "source": "batch_test",
        },
    ]
    inserted_count = await db.insert(log_entries)
    # Only valid entries should be inserted
    assert inserted_count == 2

    # Verify correct logs were inserted
    result = await db.query(fields=("message",))
    assert result["total"] == 2
    messages = {row["message"] for row in result["results"]}
    assert messages == {"Valid log 1", "Valid log 2"}


@pytest.mark.asyncio
async def test_query_empty(migrated_db: Database):
    result = await migrated_db.query()
    assert result["total"] == 0
    assert len(result["results"]) == 0
    assert result["limit"] == 100  # Default limit
    assert result["offset"] == 0


@pytest.mark.asyncio
async def test_query_pagination(migrated_db: Database):
    """Test query pagination (limit and offset)."""
    db = migrated_db
    # Insert 5 logs with slightly different timestamps for definite ordering
    base_ts = datetime.now(timezone.utc)
    total_count = 5
    log_entries = [
        {
            "timestamp": base_ts - timedelta(seconds=i),
            "level": "INFO",
            "message": f"Msg {4 - i}",
            "source": "pagination",
        }
        for i in range(total_count)  # Creates Msg 0 (latest) to Msg 4 (oldest)
    ]
    await db.insert(log_entries)

    # Query first page
    result1 = await db.query(limit=2, offset=0)
    assert result1["total"] == total_count
    assert len(result1["results"]) == 2
    assert result1["limit"] == 2
    assert result1["offset"] == 0
    assert result1["results"][0]["message"] == "Msg 4"
    assert result1["results"][1]["message"] == "Msg 3"

    # Query second page
    result2 = await db.query(limit=2, offset=2)
    assert result2["total"] == 5
    assert len(result2["results"]) == 2
    assert result2["limit"] == 2
    assert result2["offset"] == 2
    assert result2["results"][0]["message"] == "Msg 2"
    assert result2["results"][1]["message"] == "Msg 1"

    # Query beyond results
    result4 = await db.query(limit=2, offset=5)  # Offset equal to total
    assert result4["total"] == 5
    assert len(result4["results"]) == 0
    assert result4["limit"] == 2
    assert result4["offset"] == 5


@pytest.mark.asyncio
async def test_delete(migrated_db: Database):
    """Test deleting log entries based on filters."""
    db = migrated_db
    ts = datetime.now(timezone.utc)
    log_entries = [
        {
            "timestamp": ts - timedelta(seconds=2),
            "level": "INFO",
            "message": "Keep msg",
            "source": "DeleteTest",
        },
        {
            "timestamp": ts - timedelta(seconds=1),
            "level": "WARN",
            "message": "Delete msg 1",
            "source": "DeleteTest",
        },
        {
            "timestamp": ts - timedelta(seconds=0),
            "level": "ERROR",
            "message": "Delete msg 2",
            "source": "OtherSource",
        },
    ]
    await db.insert(log_entries)

    f1: list[QueryFilter] = [{"field": "message", "operator": "=", "value": "Delete msg 1"}]
    deleted_count1 = await db.delete(filters=f1)
    assert deleted_count1 == 1

    # Verify deletion 1
    result1 = await db.query()
    assert result1["total"] == 2

    # Delete with filter matching nothing
    f3: list[QueryFilter] = [{"field": "level", "operator": "=", "value": "CRITICAL"}]
    deleted_count3 = await db.delete(filters=f3)
    assert deleted_count3 == 0

    # Verify state unchanged
    result3 = await db.query()
    assert result3["total"] == 2

    # Delete remaining items
    f4: list[QueryFilter] = [{"field": "source", "operator": "!=", "value": "___________"}]
    deleted_count4 = await db.delete(filters=f4)
    assert deleted_count4 == 2

    # Verify table is empty
    result4 = await db.query()
    assert result4["total"] == 0


@pytest.mark.asyncio
async def test_delete_empty(migrated_db: Database):
    """Test deleting from an empty table."""
    f1: list[QueryFilter] = [{"field": "message", "operator": "=", "value": "Any"}]
    deleted_count = await migrated_db.delete(filters=f1)
    assert deleted_count == 0
