import sys
import orjson
import requests as rq
from typing import Callable
from loguru import logger


STDOUT_FORMAT = "<green>{time:YYYY-MM-DD HH:mm:ss.SSS}</green> | <level>{level}</level> | <cyan>{module}:{function}</cyan> | {message} <dim>{extra}</dim>"
TIME_FORMAT = "%Y-%m-%dT%H:%M:%S.%f%z"


def _default_loglite_serializer(record: dict) -> bytes:
    if not (service_name := record.get("service_name")):
        service_name = record["extra"].pop("service", "unknown")

    return orjson.dumps(
        {
            "message": record["message"],
            "timestamp": record["time"].strftime(TIME_FORMAT),
            "level": record["level"].name,
            "service": service_name,
            "pid": record["process"].id,
            "process_name": record["process"].name,
            "function": record["function"],
            "filename": record["file"].name,
            "line": record["line"],
            "extra": record["extra"],
        },
        default=str,
    )


class LogliteHandler:
    def __init__(
        self,
        host: str,
        port: int,
        record_serializer: Callable[[dict], bytes],
        timeout: float = 1,
    ):
        self.url = f"http://{host}:{port}/logs"
        self.session = rq.Session()
        self.record_serializer = record_serializer
        self.timeout = timeout

    def __call__(self, message):
        record = message.record
        data = self.record_serializer(record)
        try:
            self.session.post(self.url, data=data, timeout=self.timeout)
        except rq.exceptions.RequestException as e:
            logger.warning(f"Failed to send log to {self.url} due to: {e}")


def configure_loglite_handler(
    host: str = "localhost",
    port: int = 7788,
    timeout: float = 1,
    record_serializer: Callable[[dict], bytes] = _default_loglite_serializer,
):
    logger.remove()

    logger.add(
        sys.stderr,
        colorize=True,
        format=STDOUT_FORMAT,
    )

    handler = LogliteHandler(host, port, record_serializer, timeout)
    logger.add(handler)


"""
configure_loglite_handler()
logger.info("This will be sent to the loglite server!")
"""
