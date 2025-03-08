import orjson
import aiosqlite
from pathlib import Path
from typing import Any, Sequence
from loguru import logger
from datetime import datetime

from loglite.errors import InvalidLogEntryError
from loglite.types import Column, PaginatedQueryResult, QueryFilter


class Database:
    def __init__(self, db_path: str | Path, log_table_name: str):
        if isinstance(db_path, str):
            db_path = Path(db_path)

        self.db_path = db_path
        self.log_table_name = log_table_name
        self._column_info: list[Column] = []
        self._connection: aiosqlite.Connection | None = None

    async def get_connection(self) -> aiosqlite.Connection:
        async def connect():
            conn = await aiosqlite.connect(self.db_path)
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
        async with conn.execute(
            "SELECT version FROM versions ORDER BY version"
        ) as cursor:
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

    async def insert(self, log_data: dict[str, Any]) -> int:
        def _serialize_value(value: Any) -> Any:
            if isinstance(value, (int, float, bool, str)):
                return value
            elif isinstance(value, (datetime,)):
                return value.isoformat()
            elif isinstance(value, (dict, list)):
                return orjson.dumps(value).decode("utf-8")
            else:
                return str(value)

        """Insert a new log entry into the database"""
        columns = await self.get_log_columns()

        # Identify required columns (not null and no default value)
        for col in columns:
            is_required = (
                col["not_null"] and col["default"] is None and col["name"] != "id"
            )
            if is_required and col["name"] not in log_data:
                raise InvalidLogEntryError("Missing required field in the log data")

        # Build SQL query using only the fields present in log_data
        available_columns = [col["name"] for col in columns if col["name"] != "id"]
        insert_columns = []
        values = []
        placeholders = []

        for col_name in available_columns:
            if col_name in log_data:
                insert_columns.append(col_name)
                values.append(_serialize_value(log_data[col_name]))
                placeholders.append("?")

        # Execute the insert query
        conn = await self.get_connection()
        query = f"INSERT INTO {self.log_table_name} ({', '.join(insert_columns)}) VALUES ({', '.join(placeholders)})"
        cursor = await conn.execute(query, values)
        await conn.commit()
        return cursor.lastrowid or 0

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
            return PaginatedQueryResult(
                total=total, offset=offset, limit=limit, results=[]
            )

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
