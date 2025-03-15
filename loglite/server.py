import asyncio
from contextlib import suppress
import aiohttp_cors
from aiohttp import web
from loguru import logger

import loglite
from loglite.globals import INGESTION_STATS, QUERY_STATS
from loglite.handlers.query import SubscribeLogsSSEHandler
from loglite.database import Database
from loglite.handlers import (
    InsertLogHandler,
    QueryLogsHandler,
    HealthCheckHandler,
)
from loglite.config import Config
from loglite.tasks.diagnostics import run_diagnostics
from loglite.utils import repeat_every


class LogLiteServer:
    def __init__(self, db: Database, config: Config):
        self.config = config
        self.db = db
        self.app = web.Application()

    async def _setup_routes(self):
        route_handlers = {
            "get": {
                "/logs": QueryLogsHandler(self.db, self.config),
                "/logs/sse": SubscribeLogsSSEHandler(self.db, self.config),
                "/health": HealthCheckHandler(self.db, self.config),
            },
            "post": {
                "/logs": InsertLogHandler(self.db, self.config),
            },
        }

        for method, routes in route_handlers.items():
            for path, handler in routes.items():
                self.app.router.add_route(method, path, handler.handle_request)

        cors = aiohttp_cors.setup(
            self.app,
            defaults={
                self.config.allow_origin: aiohttp_cors.ResourceOptions(
                    allow_credentials=True,
                    expose_headers="*",
                    allow_headers="*",
                )
            },
        )

        # Apply CORS to all routes
        for route in list(self.app.router.routes()):
            cors.add(route)

        logger.info("Available endpoints: ")
        for method, routes in route_handlers.items():
            for path, handler in routes.items():
                logger.opt(colors=True).info(
                    f"\t<g>{method.upper()}: {path}: {handler.description}</g>"
                )

    async def _setup_tasks(self):
        def make_task(interval, func):
            task_func = repeat_every(seconds=interval)(func)
            return asyncio.create_task(task_func())

        async def background_tasks(app: web.Application):
            INGESTION_STATS.set_period_seconds(self.config.task_diagnostics_interval)
            QUERY_STATS.set_period_seconds(self.config.task_diagnostics_interval)

            tasks = web.AppKey("tasks", list[asyncio.Task])
            app[tasks] = [
                make_task((self.config.task_diagnostics_interval), run_diagnostics)
            ]

            yield

            for task in app[tasks]:
                task.cancel()
                with suppress(asyncio.CancelledError):
                    await task

        self.app.cleanup_ctx.append(background_tasks)

    async def setup(self):
        """Set up the server"""
        # Initialize database
        await self.db.initialize()
        await self._setup_routes()
        await self._setup_tasks()

    async def start(self):
        """Start the server"""
        runner = web.AppRunner(self.app)
        await runner.setup()
        site = web.TCPSite(runner, self.config.host, self.config.port)
        await site.start()

        logger.info(
            f"🤗 Log and roll!! 📝 Loglite server (v{loglite.__version__}) listening at {self.config.host}:{self.config.port}."
        )

        return runner, site
