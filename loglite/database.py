from __future__ import annotations
import orjson
import aiosqlite
from typing import Any, Sequence
from loguru import logger
from datetime import datetime

from loglite.config import Config
from loglite.types import Column, PaginatedQueryResult, QueryFilter


class Database:
    def __init__(self, config: Config):
        self.db_path = config.db_path
        self.log_table_name = config.log_table_name
        self.sqlite_params = config.sqlite_params
        self._column_info: list[Column] = []
        self._connection: aiosqlite.Connection | None = None

    async def get_connection(self) -> aiosqlite.Connection:
        async def connect():
            conn = await aiosqlite.connect(self.db_path)
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
        await conn.execute(
            """
            CREATE TABLE IF NOT EXISTS versions (
                version INTEGER PRIMARY KEY,
                applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """
        )
        await conn.commit()

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
            self._column_info = []
            return True
        except Exception as e:
            logger.error(f"Failed to rollback migration version {version}: {e}")
            return False

    async def get_log_columns(self) -> list[Column]:
        """Get the current columns of the log table"""
        if self._column_info:
            return self._column_info

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

    async def get_max_log_id(self) -> int:
        conn = await self.get_connection()
        async with conn.execute(f"SELECT MAX(id) FROM {self.log_table_name}") as cursor:
            res = await cursor.fetchone()
            if not res:
                return 0
            return res[0]

    async def insert(self, log_data: dict[str, Any] | list[dict[str, Any]]) -> int:
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

        """Insert a new log entry into the database"""
        columns = [col for col in await self.get_log_columns() if col["name"] != "id"]
        if isinstance(log_data, dict):
            log_data = [log_data]

        row_values = []
        for log in log_data:
            col_values = []
            valid = True
            for col in columns:
                # Check if the column is required and not present in the log
                col_value = log.get(col["name"])
                if col["not_null"] and col_value is None:
                    logger.warning(
                        f"invalid log format encountered, column {col['name']} is required but not present in log: {log}"
                    )
                    valid = False
                    break

                # Serialize the column value
                col_values.append(_serialize_value(col_value))

            if valid:
                row_values.append(col_values)

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
        fields: Sequence[str] = tuple(),
        filters: Sequence[QueryFilter] = tuple(),
        limit: int = 100,
        offset: int = 0,
    ) -> PaginatedQueryResult:
        """Query logs based on provided filters without transforming results"""
        conn = await self.get_connection()
        conn.row_factory = aiosqlite.Row

        # Build query conditions
        conditions = []
        params = []

        for filter_item in filters:
            field = filter_item["field"]
            operator = filter_item["operator"]
            value = filter_item["value"]

            if operator == "~=":
                # Convert ~= operator to LIKE
                conditions.append(f"{field} LIKE ?")
                params.append(f"%{value}%")  # Add wildcards for partial matching
            else:
                # Map other operators directly to SQL
                conditions.append(f"{field} {operator} ?")
                params.append(value)

        # Construct the WHERE clause
        where_clause = " AND ".join(conditions) if conditions else "1=1"

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
        query = f"""
            SELECT {", ".join(fields)}
            FROM {self.log_table_name}
            WHERE {where_clause}
            ORDER BY timestamp DESC
            LIMIT ? OFFSET ?
        """

        # Add pagination params
        params.append(limit)
        params.append(offset)

        # Execute query and fetch results
        async with conn.execute(query, params) as cursor:
            rows = await cursor.fetchall()
            return PaginatedQueryResult(
                total=total,
                offset=offset,
                limit=limit,
                results=[dict(row) for row in rows],
            )

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
