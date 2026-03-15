from __future__ import annotations

import threading
from pathlib import Path
from typing import Any

import aiosqlite
from loguru import logger


class Connection:
    def __init__(self, conn: aiosqlite.Connection):
        self.conn = conn

    @classmethod
    async def connect(cls, db_path: Path, sqlite_params: dict[str, Any]) -> Connection:
        conn = await aiosqlite.connect(db_path)

        # Ensure vacuuming mode
        res = await conn.execute("PRAGMA auto_vacuum")
        row = await res.fetchone()
        if not row:
            current_mode = "NONE"
        else:
            current_mode = {0: "NONE", 1: "FULL", 2: "INCREMENTAL"}[row[0]]

        target_mode = sqlite_params.pop("auto_vacuum", current_mode).upper()

        if target_mode != current_mode:
            logger.info(f"Changing auto_vacuum mode: {current_mode} => {target_mode}")
            await conn.execute(stmt := f"PRAGMA auto_vacuum={target_mode}")
            await conn.execute("VACUUM")
            logger.info(stmt)

        # Set other params
        for param, value in sqlite_params.items():
            statement = f"PRAGMA {param}={value}"
            logger.info(statement)
            await conn.execute(statement)

        conn.row_factory = aiosqlite.Row
        return cls(conn)

    async def close(self):
        await self.conn.close()

    def is_alive(self) -> bool:
        if isinstance(self.conn, threading.Thread):
            return self.conn.is_alive()

        return self.conn._running


class ConnectionInstance:
    _instance: Connection | None = None

    @classmethod
    async def get(cls, db_path: Path, sqlite_params: dict[str, Any]) -> Connection:
        if cls._instance is None:
            cls._instance = await Connection.connect(db_path, sqlite_params)

        if not cls._instance.is_alive():
            logger.info(f"👀 Reconnecting to {db_path}")
            await cls._instance.close()
            cls._instance = await Connection.connect(db_path, sqlite_params)
            logger.info(f"🔌 Reconnected to {db_path}")

        return cls._instance

    @classmethod
    async def close(cls):
        if cls._instance:
            await cls._instance.close()
            cls._instance = None
            logger.info("👋 Closed database connection")
