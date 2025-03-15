import asyncio
import re
import orjson
import time
from typing import get_args
from loguru import logger
from aiohttp import web
from aiohttp_sse import sse_response

from loglite.errors import RequestValidationError
from loglite.handlers import RequestHandler
from loglite.handlers.utils import LAST_INSERT_LOG_ID
from loglite.types import QueryFilter, QueryOperator


class QueryLogsHandler(RequestHandler[list[QueryFilter]]):
    description = "query logs"
    filter_regex = re.compile(r"(>=|<=|!=|~=|=|>|<)([^,]+)")
    valid_operators = set(get_args(QueryOperator))

    def _to_query_filters(self, field: str, filter_expr: str) -> list[QueryFilter]:
        matches = re.findall(self.filter_regex, filter_expr)
        return [
            {"field": field, "operator": op, "value": value.strip()}
            for op, value in matches
        ]

    async def validate_request(self, request: web.Request) -> list[QueryFilter]:
        non_filter_params = ["fields", "offset", "limit"]
        query_filters = []

        for field, filter_expr in request.query.items():
            if field in non_filter_params:
                continue

            filters = self._to_query_filters(field, filter_expr)
            if not filters:
                raise RequestValidationError(
                    f"Field '{field}' has invalid filter expression: {filter_expr}"
                )
            query_filters.extend(filters)

        return query_filters

    async def handle(self, request) -> web.Response:
        assert request.validated_data is not None
        _fields = request.query.get("fields", "*")
        if _fields == "*":
            fields = ["*"]
        else:
            fields = _fields.split(",")

        offset = int(request.query.get("offset", 0))
        limit = int(request.query.get("limit", 100))

        if self.verbose:
            logger.info(
                f"Query fields={fields}, offset={offset}, limit={limit}, filters={request.validated_data}"
            )

        try:
            result = await self.db.query(
                fields, request.validated_data, offset=offset, limit=limit
            )
            return self.response_ok(result)
        except Exception as e:
            logger.exception("Error querying logs")
            return self.response_fail(str(e), status=500)


class SubscribeLogsSSEHandler(RequestHandler):
    description = "subscribe to current log"

    async def handle(self, request: web.Request) -> web.StreamResponse:
        pushed_log_id: int = (await LAST_INSERT_LOG_ID.get()) or 0
        pushed_timestamp = 0
        last_log_id: int = 0
        new_log_event = LAST_INSERT_LOG_ID.subscribe()
        subscriber_id = id(new_log_event)
        logger.info(
            f"New log subscriber. ID={subscriber_id}, Subscribers count={LAST_INSERT_LOG_ID.get_subscribers_count()}"
        )

        try:
            async with sse_response(request) as resp:
                while resp.is_connected():
                    # Wait for new logs to arrive (with timeout)
                    try:
                        await asyncio.wait_for(
                            new_log_event.wait(), timeout=self.sse_debounce_ms * 1e-3
                        )
                        last_log_id = (await LAST_INSERT_LOG_ID.get()) or 0
                        new_log_event.clear()
                        if self.verbose:
                            logger.info(
                                f"> subscriber {subscriber_id} noticed new log: {last_log_id}"
                            )
                    except asyncio.TimeoutError:
                        pass

                    now = time.monotonic()
                    elapsed_ms = (now - pushed_timestamp) * 1e3
                    if elapsed_ms < self.config.sse_debounce_ms:
                        # Debounced, if there is new log, it should be pushed later
                        continue

                    if last_log_id <= pushed_log_id:
                        # Nothing new happened, nor was any new log not pushed yet
                        continue

                    # Query and push new logs
                    logs = await self.db.query(
                        fields=["*"],
                        filters=[
                            {
                                "field": "id",
                                "operator": ">",
                                "value": pushed_log_id,
                            }
                        ],
                        limit=self.sse_limit,
                    )

                    if self.verbose:
                        logger.info(
                            f"> pushing logs to subscriber {subscriber_id}. Logs count={len(logs['results'])}"
                        )

                    data = orjson.dumps(logs["results"]).decode("utf-8")
                    await resp.send(data)
                    pushed_log_id = last_log_id
                    pushed_timestamp = now
        finally:
            LAST_INSERT_LOG_ID.unsubscribe(new_log_event)
            logger.info(
                f"Log subscriber disconnected. ID={id(new_log_event)}, Subscribers count={LAST_INSERT_LOG_ID.get_subscribers_count()}"
            )

        return resp
