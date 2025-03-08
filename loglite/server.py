import aiohttp_cors
from aiohttp import web
from loguru import logger

from .database import Database
from .handlers import InsertLogHandler, QueryLogsHandler, HealthCheckHandler
from .config import Config


class LogLiteServer:
    def __init__(self, db: Database, config: Config):
        self.config = config
        self.db = db
        self.app = web.Application()

    async def setup(self):
        """Set up the server"""
        # Initialize database
        await self.db.initialize()

        # Set up routes
        route_handlers = {
            "get": {
                "/logs": QueryLogsHandler(self.db, self.config.debug),
                "/health": HealthCheckHandler(self.db, self.config.debug),
            },
            "post": {
                "/logs": InsertLogHandler(self.db, self.config.debug),
            },
        }

        for method, routes in route_handlers.items():
            for path, handler in routes.items():
                self.app.router.add_route(method, path, handler.handle)

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

    async def start(self):
        """Start the server"""
        runner = web.AppRunner(self.app)
        await runner.setup()
        site = web.TCPSite(runner, self.config.host, self.config.port)
        await site.start()

        logger.info(
            f"🤗 Log and roll!! 📝 Loglite server listening at {self.config.host}:{self.config.port}."
        )

        return runner, site
