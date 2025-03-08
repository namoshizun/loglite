from typing import Dict, Any
import orjson
import logging
import aiosqlite
from aiohttp import web

from .database import Database

logger = logging.getLogger(__name__)


class LogHandler:
    def __init__(self, db: Database):
        self.db = db

    async def insert_log(self, request: web.Request) -> web.Response:
        """Handle POST /logs request to insert a new log"""
        try:
            body = await request.read()
            log_data = orjson.loads(body)

            # Validate required fields
            if "message" not in log_data:
                return web.Response(
                    status=400,
                    body=orjson.dumps({"error": "Missing required field 'message'"}),
                    content_type="application/json",
                )

            if "level" not in log_data:
                return web.Response(
                    status=400,
                    body=orjson.dumps({"error": "Missing required field 'level'"}),
                    content_type="application/json",
                )

            if "service" not in log_data:
                return web.Response(
                    status=400,
                    body=orjson.dumps({"error": "Missing required field 'service'"}),
                    content_type="application/json",
                )

            # Insert log into database
            log_id = await self.db.insert_log(log_data)

            return web.Response(
                body=orjson.dumps({"id": log_id, "status": "success"}),
                content_type="application/json",
            )
        except Exception as e:
            logger.exception("Error inserting log")
            return web.Response(
                status=400,
                body=orjson.dumps({"error": str(e)}),
                content_type="application/json",
            )

    async def query_logs(self, request: web.Request) -> web.Response:
        """Handle GET /logs request to query logs"""
        try:
            # Parse query parameters
            filters = {}
            for key, value in request.query.items():
                if key in ["limit", "offset"]:
                    try:
                        filters[key] = int(value)
                    except ValueError:
                        pass
                else:
                    filters[key] = value

            # Get limit and offset parameters
            limit = int(filters.pop("limit", 100))
            offset = int(filters.pop("offset", 0))

            # Query logs from database
            logs = await self.db.query_logs(filters, limit, offset)

            return web.Response(
                body=orjson.dumps(
                    {"logs": logs, "count": len(logs), "offset": offset, "limit": limit}
                ),
                content_type="application/json",
            )
        except Exception as e:
            logger.exception("Error querying logs")
            return web.Response(
                status=400,
                body=orjson.dumps({"error": str(e)}),
                content_type="application/json",
            )

    async def health_check(self, request: web.Request) -> web.Response:
        """Handle GET /health request for health check"""
        try:
            # Check database connection
            async with aiosqlite.connect(self.db.db_path) as db:
                await db.execute("SELECT 1")

            return web.Response(
                body=orjson.dumps({"status": "healthy"}),
                content_type="application/json",
            )
        except Exception as e:
            logger.exception("Health check failed")
            return web.Response(
                status=500,
                body=orjson.dumps({"status": "unhealthy", "error": str(e)}),
                content_type="application/json",
            )
