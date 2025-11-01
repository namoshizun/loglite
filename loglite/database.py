from __future__ import annotations
import sys
import orjson
import aiosqlite
from typing import Any, Literal, Sequence
from pathlib import Path
from loguru import logger
from datetime import datetime
from contextlib import suppress

from loglite.config import Config
from loglite.types import Column, PaginatedQueryResult, QueryFilter
from loglite.column_dict import ColumnDictionary
from loglite.migrations import MigrationManager
from loglite.utils import bytes_to_mb


def _serialize_value(value: Any) -> Any:
    if value is None:
        return None
    if isinstance(value, (int, float, bool, str)):
        return value
    if isinstance(value, (datetime,)):
        return value.isoformat()
    if isinstance(value, (dict, list)):
        return orjson.dumps(value).decode("utf-8")
    return str(value)


class Database:
    def __init__(self, config: Config):
        self.config = config
        self._column_info: list[Column] = []
        self._connection: aiosqlite.Connection | None = None
        self._compressed_columns: set[str] = (
            set() if not config.compression["enabled"] else set(config.compression["columns"])
        )

    @property
    def db_path(self) -> Path:
        return self.config.db_path

    @property
    def log_table_name(self) -> str:
        return self.config.log_table_name

    @property
    def sqlite_params(self) -> dict[str, Any]:
        return self.config.sqlite_params

    @property
    def auto_rollout(self) -> bool:
        return self.config.auto_rollout

    @property
    def column_dict(self) -> ColumnDictionary:
        if not hasattr(self, "_column_dict"):
            raise RuntimeError("Database not initialized, please call initialize() first")
        return self._column_dict

    @column_dict.setter
    def column_dict(self, value: ColumnDictionary):
        self._column_dict = value

    @property
    def column_info(self) -> list[Column]:
        if self._column_info:
            return self._column_info

        raise RuntimeError("Database not initialized, please call initialize() first")

    async def _fetch_columns_info(self) -> list[Column]:
        conn = await self.get_connection()
        async with conn.execute(f"PRAGMA table_info({self.log_table_name})") as cursor:
            columns = await cursor.fetchall()

            # SQLite PRAGMA table_info returns:
            # (cid, name, type, notnull, dflt_value, pk)
            self._column_info = [
                {
                    "name": col[1],
                    "type": col[2],
                    "not_null": bool(col[3]),
                    "default": col[4],
                    "primary_key": bool(col[5]),
                }
                for col in columns
            ]
            return self._column_info

    def _build_sql_query(self, filters: Sequence[QueryFilter]) -> tuple[str, list[Any]]:
        conditions = []
        params = []

        for ft in filters:
            col = ft["field"]
            operator = ft["operator"]
            value = ft["value"]

            if col in self._compressed_columns:
                # Do query in-memory if the column is compressed
                candidate_ids = self.column_dict.query_candidates(ft)
                if not candidate_ids:
                    # Then we know immediately that there are no matching rows given the
                    # current filters, which saves us from having to execute the query at all!
                    return "1=0", []

                conditions.append(f"{col} IN ({', '.join(['?'] * len(candidate_ids))})")
                params.extend(candidate_ids)

            else:
                # Run SQL as usual for non-compressed columns
                if operator == "~=":
                    conditions.append(f"{col} LIKE ?")
                    params.append(f"%{value}%")
                else:
                    conditions.append(f"{col} {operator} ?")
                    params.append(value)

        # Construct the WHERE clause
        where_clause = " AND ".join(conditions) if conditions else "1=1"
        return where_clause, params

    async def get_connection(self) -> aiosqlite.Connection:
        async def connect():
            conn = await aiosqlite.connect(self.db_path)

            # NOTE: changing auto_vacuuming requires a full VACUUM operation.
            res = await conn.execute("PRAGMA auto_vacuum")
            current_vacuuming_mode = {
                0: "NONE",
                1: "FULL",
                2: "INCREMENTAL",
            }[(await res.fetchone())[0]]
            set_vacuuming_mode = self.sqlite_params.pop(
                "auto_vacuum", current_vacuuming_mode
            ).upper()

            if set_vacuuming_mode != current_vacuuming_mode:
                logger.info(
                    f"Changing auto_vacuuming mode: {current_vacuuming_mode} => {set_vacuuming_mode}"
                )
                await conn.execute(statement := f"PRAGMA auto_vacuum={set_vacuuming_mode}")
                await conn.execute("VACUUM")
                logger.info(statement)

            # Set other params
            for param, value in self.sqlite_params.items():
                statement = f"PRAGMA {param}={value}"
                logger.info(statement)
                try:
                    await conn.execute(statement)
                except Exception as e:
                    logger.error(f"Failed to set SQLite parameter {param}: {e}")

            conn.row_factory = aiosqlite.Row
            return conn

        if self._connection is None:
            self._connection = await connect()
            logger.info(f"🔌 Connected to {self.db_path}")

        if not self._connection.is_alive():
            logger.info(f"👀 Reconnecting to {self.db_path}")
            await self._connection.close()
            self._connection = await connect()
            logger.info(f"🔌 Reconnected to {self.db_path}")
        return self._connection

    async def initialize(self):
        """Initialize the database connection and ensure versions table exists"""
        conn = await self.get_connection()

        # [1] Create internal loglite tables
        await conn.execute(
            """
            CREATE TABLE IF NOT EXISTS versions (
                version INTEGER PRIMARY KEY,
                applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """
        )
        await conn.execute(
            """
            CREATE TABLE IF NOT EXISTS column_dictionary (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                column TEXT NOT NULL,
                value_id INTEGER NOT NULL,
                value JSON
            )
        """
        )
        await conn.commit()

        # [2] Apply pending migrations in auto mode
        if self.auto_rollout:
            migration_manager = MigrationManager(self, self.config.migrations)
            await migration_manager.apply_pending_migrations()

        # [3] Prefetching
        await self._fetch_columns_info()
        self.column_dict = await ColumnDictionary.load(self)

    async def get_pragma(self, name: str) -> Any:
        conn = await self.get_connection()
        async with conn.execute(f"PRAGMA {name}") as cursor:
            result = await cursor.fetchone()
            if not result:
                return None
            return result[name]

    async def set_pragma(self, name: str, value: Any):
        conn = await self.get_connection()
        await conn.execute(f"PRAGMA {name}={value}")

    async def incremental_vacuum(self, page_count: int):
        conn = await self.get_connection()
        async with conn.execute(f"PRAGMA incremental_vacuum({page_count})") as cursor:
            await cursor.fetchall()

    async def get_applied_versions(self) -> list[int]:
        """Get the list of already applied migration versions"""
        conn = await self.get_connection()
        async with conn.execute("SELECT version FROM versions ORDER BY version") as cursor:
            versions = [row[0] for row in await cursor.fetchall()]
            return versions

    async def apply_migration(self, version: int, statements: list[str]) -> bool:
        """Apply a migration version"""
        try:
            conn = await self.get_connection()
            # Skip if the version is already applied
            if version in await self.get_applied_versions():
                logger.info(f"🤷‍♂️ Migration version {version} already applied")
                return True

            for statement in statements:
                await conn.execute(statement)

            # Record the applied version
            await conn.execute("INSERT INTO versions (version) VALUES (?)", (version,))
            await conn.commit()
            logger.info(f"🥷 Applied migration version {version}")
            # Invalidate the column info cache in case the table schema changed
            await self._fetch_columns_info()
            return True
        except Exception as e:
            logger.error(f"Failed to apply migration version {version}: {e}")
            return False

    async def rollback_migration(self, version: int, statements: list[str]) -> bool:
        """Rollback a migration version"""
        try:
            conn = await self.get_connection()
            for statement in statements:
                await conn.execute(statement)

            # Remove the version record
            await conn.execute("DELETE FROM versions WHERE version = ?", (version,))
            await conn.commit()
            logger.info(f"🚮 Rolled back migration version {version}")
            # Invalidate the column info cache in case the table schema changed
            await self._fetch_columns_info()
            return True
        except Exception as e:
            logger.error(f"Failed to rollback migration version {version}: {e}")
            return False

    async def get_max_log_id(self) -> int:
        conn = await self.get_connection()
        async with conn.execute(f"SELECT MAX(id) FROM {self.log_table_name}") as cursor:
            res = await cursor.fetchone()
            with suppress(Exception):
                return res[0] or 0
            return 0

    async def get_min_log_id(self) -> int:
        conn = await self.get_connection()
        async with conn.execute(f"SELECT MIN(id) FROM {self.log_table_name}") as cursor:
            res = await cursor.fetchone()
            with suppress(Exception):
                return res[0] or 0
            return 0

    async def get_min_timestamp(self) -> datetime:
        conn = await self.get_connection()
        async with conn.execute(f"SELECT MIN(timestamp) FROM {self.log_table_name}") as cursor:
            res = await cursor.fetchone()
            if not res or res[0] is None:
                return datetime(1900, 1, 1)

            if sys.version_info >= (3, 11):
                return datetime.fromisoformat(res[0])

            for fmt in (
                "%Y-%m-%dT%H:%M:%S.%f",
                "%Y-%m-%dT%H:%M:%S.%f%z",
                "%Y-%m-%dT%H:%M:%S",
            ):
                with suppress(Exception):
                    return datetime.strptime(res[0], fmt)
            raise ValueError(f"Failed to parse timestamp: {res[0]}")

    async def insert(
        self,
        log_data: dict[str, Any] | Sequence[dict[str, Any]],
    ) -> int:
        """Insert a new log entry into the database"""
        columns = [col for col in self.column_info if col["name"] != "id"]
        if isinstance(log_data, dict):
            log_data = [log_data]

        row_values = []
        for log in log_data:
            values, valid = [], True
            for col in columns:
                # Check if the column is required and not present in the log
                col_value = log.get(col["name"])
                if col["not_null"] and col_value is None:
                    logger.warning(
                        f"invalid log format encountered, column {col['name']} is required but not present in log: {log}"
                    )
                    valid = False
                    break

                # Serialize the column value. Store the value id if it is compressed.
                col_value = _serialize_value(col_value)
                if col["name"] in self._compressed_columns:
                    col_value = self.column_dict.get_or_create(col["name"], col_value)

                values.append(col_value)

            if valid:
                row_values.append(values)

        if not row_values:
            return 0

        # Execute the insert query
        col_names = [col["name"] for col in columns]
        conn = await self.get_connection()
        statement = f"INSERT INTO {self.log_table_name} ({', '.join(col_names)}) VALUES ({', '.join(['?'] * len(col_names))})"
        async with conn.executemany(statement, row_values) as cursor:
            await conn.commit()
            return cursor.rowcount or 0

    async def query(
        self,
        fields: Sequence[str] = ("*",),
        filters: Sequence[QueryFilter] = tuple(),
        limit: int = 100,
        offset: int = 0,
    ) -> PaginatedQueryResult:
        if fields == ("*",):
            fields = tuple(col["name"] for col in self.column_info)

        """Query logs based on provided filters without transforming results"""
        conn = await self.get_connection()

        # Build query conditions
        where_clause, params = self._build_sql_query(filters)

        # First, get the total count of logs matching the filters
        count_query = f"""
            SELECT COUNT(id)
            FROM {self.log_table_name}
            WHERE {where_clause}
        """
        async with conn.execute(count_query, params) as cursor:
            total = (await cursor.fetchone())[0]

        if total == 0:
            return PaginatedQueryResult(total=total, offset=offset, limit=limit, results=[])

        # Build the complete query
        params.append(limit)
        params.append(offset)
        query = f"""
            SELECT {", ".join(fields)}
            FROM {self.log_table_name}
            WHERE {where_clause}
            ORDER BY timestamp DESC
            LIMIT ? OFFSET ?
        """

        # Execute query and fetch results
        async with conn.execute(query, params) as cursor:
            rows = await cursor.fetchall()
            return PaginatedQueryResult(
                total=total,
                offset=offset,
                limit=limit,
                results=[
                    {
                        col: (
                            row[col]
                            if col not in self._compressed_columns
                            else self.column_dict.get_value(col, row[col])
                        )
                        for col in fields
                    }
                    for row in rows
                ],
            )

    async def delete(self, filters: Sequence[QueryFilter]) -> int:
        """Delete logs based on provided filters"""
        conn = await self.get_connection()
        where_clause, params = self._build_sql_query(filters)
        async with conn.execute(
            f"DELETE FROM {self.log_table_name} WHERE {where_clause}", params
        ) as cursor:
            await conn.commit()
            return cursor.rowcount or 0

    async def vacuum(self):
        conn = await self.get_connection()
        async with conn.cursor() as cursor:
            await cursor.execute("VACUUM")

    async def wal_checkpoint(self, mode: Literal["TRUNCATE", "PASSIVE", "FULL"] = "TRUNCATE"):
        conn = await self.get_connection()
        await conn.execute(f"PRAGMA wal_checkpoint({mode})")

    async def get_size_mb(self) -> float:
        page_count = await self.get_pragma("page_count")
        page_size = await self.get_pragma("page_size")
        freelist_count = await self.get_pragma("freelist_count")
        total_size = (page_count - freelist_count) * page_size
        return bytes_to_mb(total_size)

    async def get_column_dict_table(self) -> list[aiosqlite.Row]:
        conn = await self.get_connection()
        async with conn.execute("SELECT * FROM column_dictionary") as cursor:
            return list(await cursor.fetchall())

    async def insert_column_dict_value(self, col: str, value: Any, id: int):
        conn = await self.get_connection()
        async with conn.execute(
            "INSERT INTO column_dictionary (column, value, value_id) VALUES (?, ?, ?)",
            (col, value, id),
        ) as cursor:
            await conn.commit()
            return cursor.rowcount or 0

    async def ping(self) -> bool:
        try:
            conn = await self.get_connection()
            await conn.execute("SELECT 1")
            return True
        except Exception as e:
            logger.error(f"Failed to ping database: {e}")
            return False

    async def close(self):
        if self._connection:
            await self._connection.close()
            logger.info(f"👋 Closed connection to {self.db_path}")
            self._connection = None

    async def __aenter__(self):
        await self.get_connection()
        return self

    async def __aexit__(self, exc_type, exc_value, traceback):
        await self.close()
