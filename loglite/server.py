import logging
from aiohttp import web
import aiohttp_cors

from .database import Database
from .migrations import MigrationManager
from .handlers import LogHandler

logger = logging.getLogger(__name__)


class LoggingServer:
    def __init__(self, config):
        self.config = config
        self.db = Database(config.db_path, config.log_table_name)
        self.migration_manager = MigrationManager(self.db, config.migrations)
        self.handler = LogHandler(self.db)
        self.app = web.Application()

    async def setup(self):
        """Set up the server"""
        # Initialize database
        await self.db.initialize()

        # Apply migrations
        success = await self.migration_manager.apply_pending_migrations()
        if not success:
            logger.error("Failed to apply all migrations")

        # Set up routes
        self.app.add_routes(
            [
                web.post("/logs", self.handler.insert_log),
                web.get("/logs", self.handler.query_logs),
                web.get("/health", self.handler.health_check),
            ]
        )

        # Set up CORS
        cors = aiohttp_cors.setup(
            self.app,
            defaults={
                "*": aiohttp_cors.ResourceOptions(
                    allow_credentials=True,
                    expose_headers="*",
                    allow_headers="*",
                )
            },
        )

        # Apply CORS to all routes
        for route in list(self.app.router.routes()):
            cors.add(route)

    async def start(self):
        """Start the server"""
        runner = web.AppRunner(self.app)
        await runner.setup()
        site = web.TCPSite(runner, self.config.host, self.config.port)
        await site.start()

        logger.info(
            f"Logging server started at http://{self.config.host}:{self.config.port}"
        )

        return runner, site
