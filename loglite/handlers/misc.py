from aiohttp import web
from loguru import logger

from loglite.handlers import RequestHandler


class HealthCheckHandler(RequestHandler):
    description = "probe database connection"

    async def handle(self, request: web.Request) -> web.Response:
        try:
            await self.db.ping()
            return self.response_ok("ok")
        except Exception as e:
            logger.exception("Health check failed")
            return self.response_fail(str(e), status=500)
