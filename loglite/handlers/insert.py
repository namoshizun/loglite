import orjson
from loguru import logger
from aiohttp import web

from loglite.errors import InvalidLogEntryError
from loglite.handlers import RequestHandler
from loglite.handlers.utils import LAST_INSERT_LOG_ID


class InsertLogHandler(RequestHandler):

    description = "insert a new log"

    async def handle(self, request: web.Request) -> web.Response:
        try:
            body = await request.read()
            log_data = orjson.loads(body)

            if self.verbose:
                logger.info(f"Inserting log: {log_data}")

            try:
                log_id = await self.db.insert(log_data)
            except InvalidLogEntryError as e:
                return self.response_fail(str(e))

            await LAST_INSERT_LOG_ID.set(log_id)
            return self.response_ok({"id": log_id})

        except Exception as e:
            logger.exception("Error inserting log")
            return self.response_fail(str(e), status=500)
