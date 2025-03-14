import re
from typing import get_args
from loguru import logger
from aiohttp import web

from loglite.errors import RequestValidationError
from loglite.handlers import RequestHandler
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
