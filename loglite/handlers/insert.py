import orjson
from aiohttp import web
from loguru import logger

from loglite.globals import BACKLOG
from loglite.handlers import RequestHandler


class InsertLogHandler(RequestHandler):
    description = "insert a new log"

    async def handle(self, request: web.Request) -> web.Response:
        try:
            body = await request.read()
            log_data = orjson.loads(body)

            if self.verbose:
                logger.info(f"Inserting log: {log_data}")

            try:
                await BACKLOG.instance().add(log_data)
            except Exception as e:
                return self.response_fail(str(e))

            return self.response_ok("ok")

        except Exception as e:
            logger.exception("Error inserting log")
            return self.response_fail(str(e), status=500)
