from __future__ import annotations
import abc
import orjson
from typing import Any, Generic, TypeVar
from loguru import logger
from aiohttp import web

from loglite.config import Config
from loglite.errors import RequestValidationError
from loglite.database import Database


Data = TypeVar("Data", bound=Any)


class Request(web.Request, Generic[Data]):

    @property
    def validated_data(self) -> Data | None:
        try:
            return self._validated_data
        except AttributeError:
            return None

    @validated_data.setter
    def validated_data(self, value: Data):
        self._validated_data = value


class RequestHandler(abc.ABC, Generic[Data]):
    description: str

    def __init__(self, db: Database, config: Config):
        self.db = db
        self.config = config

    @property
    def sse_limit(self) -> int:
        return self.config.sse_limit

    @property
    def sse_debounce_ms(self) -> int:
        return self.config.sse_debounce_ms

    @property
    def verbose(self) -> bool:
        return self.config.debug

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

    async def validate_request(self, request: web.Request) -> Data:
        raise NotImplementedError

    @abc.abstractmethod
    async def handle(self, request: Request[Data]) -> web.Response:
        raise NotImplementedError

    async def handle_request(self, request: web.Request) -> web.Response:
        try:
            request.validated_data = await self.validate_request(request)
        except NotImplementedError:
            pass
        except RequestValidationError as e:
            return self.response_fail(f"Invalid request. Details: {e}", status=400)
        except Exception as e:
            logger.exception("Error validating request")
            return self.response_fail(f"Request validation failed: {e}", status=500)

        try:
            return await self.handle(request)  # type: ignore
        except Exception as e:
            logger.exception("Error handling request")
            return self.response_fail(
                f"Unknown request handling error: {e}", status=500
            )


from .insert import InsertLogHandler
from .query import QueryLogsHandler
from .misc import HealthCheckHandler
