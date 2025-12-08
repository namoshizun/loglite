import sys
import asyncio
import aiohttp_cors
from contextlib import suppress
from aiohttp import web
from loguru import logger

import loglite
from loglite.backlog import Backlog
from loglite.globals import BACKLOG, INGESTION_STATS, QUERY_STATS
from loglite.handlers.query import SubscribeLogsSSEHandler
from loglite.database import Database
from loglite.handlers import (
    InsertLogHandler,
    QueryLogsHandler,
    HealthCheckHandler,
)
from loglite.config import Config
from loglite.tasks import (
    register_diagnostics_task,
    register_flushing_backlog_task,
    register_database_vacuuming_task,
)
from loglite.harvesters import HarvesterManager


class LogLiteServer:
    def __init__(self, db: Database, config: Config):
        self.config = config
        self.db = db
        self.app = web.Application()
        self.harvester_manager = HarvesterManager()

    def _setup_harvesters(self):
        self.harvester_manager.load_harvesters(self.config.harvesters)

    def _setup_logging(self):
        logger.remove()
        logger.add(
            sys.stdout,
            level="INFO" if not self.config.debug else "DEBUG",
            colorize=True,
            format="<green>{time:YYYY-MM-DD HH:mm:ss.SSS}</green> | <level>{level}</level> | <cyan>{module}:{function}</cyan> | {message} <dim>{extra}</dim>",
        )

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
        async def background_tasks(app: web.Application):
            tasks = web.AppKey("tasks", list[asyncio.Task])
            app[tasks] = [
                asyncio.create_task(register_diagnostics_task(self.config)),
                asyncio.create_task(register_flushing_backlog_task(self.db, self.config)),
                asyncio.create_task(register_database_vacuuming_task(self.db, self.config)),
            ]

            yield

            for task in app[tasks]:
                task.cancel()
                with suppress(asyncio.CancelledError):
                    await task

        self.app.cleanup_ctx.append(background_tasks)

    async def _setup_globals(self):
        INGESTION_STATS.set_period_seconds(self.config.task_diagnostics_interval)
        QUERY_STATS.set_period_seconds(self.config.task_diagnostics_interval)
        BACKLOG.set(Backlog(self.config.task_backlog_max_size))

    async def setup(self):
        """Set up the server"""
        # Initialize database
        self._setup_logging()
        self._setup_harvesters()
        await self._setup_globals()
        await self._setup_routes()
        await self._setup_tasks()
        await self.harvester_manager.start_all()

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
