import abc
import re
import orjson
from typing import Any, get_args
from loguru import logger
from aiohttp import web

from loglite.errors import InvalidLogEntryError
from loglite.types import QueryFilter, QueryOperator
from loglite.database import Database


class RequestHandler(abc.ABC):
    description: str

    def __init__(self, db: Database, verbose: bool):
        self.db = db
        self.verbose = verbose

    def response_ok(self, payload: Any, status: int = 200) -> web.Response:
        return web.Response(
            body=orjson.dumps({"status": "success", "result": payload}),
            content_type="application/json",
            status=status,
        )

    def response_fail(self, message: str, status: int = 400) -> web.Response:
        return web.Response(
            body=orjson.dumps(
                {
                    "status": "error",
                    "error": message,
                }
            ),
            content_type="application/json",
            status=status,
        )

    @abc.abstractmethod
    def handle(self, request: web.Request) -> web.Response:
        raise NotImplementedError


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

            return self.response_ok({"id": log_id})

        except Exception as e:
            logger.exception("Error inserting log")
            return self.response_fail(str(e), status=500)


class QueryLogsHandler(RequestHandler):
    description = "query logs"
    filter_regex = re.compile(r"^(?P<operator>[\!=<>~]+)(?P<value>.+)$")
    valid_operators = set(get_args(QueryOperator))

    def _to_query_filters(self, query_params: dict[str, str]) -> list[QueryFilter]:
        filters = []

        for field, spec in query_params.items():
            match = self.filter_regex.match(spec)
            if not match:
                raise ValueError(f"Invalid filter spec: {spec}")

            operator, value = match.groups()
            if operator not in self.valid_operators:
                raise ValueError(f"Invalid operator: {operator}")

            filters.append(
                {
                    "field": field,
                    "operator": operator,
                    "value": value,
                }
            )

        return filters

    async def handle(self, request: web.Request) -> web.Response:
        query_params = dict(request.query.items())
        if self.verbose:
            logger.info(f"Querying logs: {query_params}")

        try:
            _fields = query_params.pop("fields", "*")
            if _fields == "*":
                fields = ["*"]
            else:
                fields = _fields.split(",")

            offset = int(query_params.pop("offset", 0))
            limit = int(query_params.pop("limit", 100))
            query_filters = self._to_query_filters(query_params)
        except Exception as e:
            return self.response_fail(str(e), status=400)

        try:
            result = await self.db.query(
                fields, query_filters, offset=offset, limit=limit
            )
            return self.response_ok(result)
        except Exception as e:
            logger.exception("Error querying logs")
            return self.response_fail(str(e), status=500)


class HealthCheckHandler(RequestHandler):
    description = "probe database connection"

    async def handle(self, request: web.Request) -> web.Response:
        try:
            await self.db.ping()
            return self.response_ok("ok")
        except Exception as e:
            logger.exception("Health check failed")
            return self.response_fail(str(e), status=500)
