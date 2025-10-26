from __future__ import annotations
import time
import asyncio
from dataclasses import dataclass, asdict
from functools import wraps
from typing import Any, Generic, Literal, TypeVar, get_args
from loguru import logger

SizeUnit = Literal["B", "KB", "MB", "GB", "TB"]
T = TypeVar("T")


def bytes_to_mb(bytes: int) -> float:
    return round(bytes / (1024 * 1024), 2)


def convert_size_to_bytes(value: int, unit: str):
    if unit not in get_args(SizeUnit):
        raise ValueError(f"Invalid unit: {unit}")

    multiplier = 1024 ** get_args(SizeUnit).index(unit)
    return value * multiplier


def repeat_every(
    *,
    seconds: float,
    wait_first: float | None = None,
    on_complete=None,
    on_exception=logger.exception,
):
    """
    Copied and modified from:
    https://github.com/fastapiutils/fastapi-utils/blob/master/fastapi_utils/tasks.py

    This function returns a decorator that modifies a function so it is periodically re-executed after its first call.

    The function it decorates should accept no arguments and return nothing. If necessary, this can be accomplished
    by using `functools.partial` or otherwise wrapping the target function prior to decoration.

    Parameters
    ----------
    seconds: float
        The number of seconds to wait between repeated calls
    wait_first: float (default None)
        If not None, the function will wait for the given duration before the first call
    on_complete: Optional[Callable[[], None]] (default None)
        A function to call after the final repetition of the decorated function.
    on_exception: Optional[Callable[[Exception], None]] (default None)
        A function to call when an exception is raised by the decorated function.
    """

    async def _handle_exc(exc: Exception, on_exception) -> None:
        if on_exception:
            if asyncio.iscoroutinefunction(on_exception):
                await on_exception(exc)
            else:
                on_exception(exc)

    def decorator(func):
        """
        Converts the decorated function into a repeated, periodically-called version of itself.
        """

        @wraps(func)
        async def wrapped():
            async def loop():
                if wait_first is not None:
                    await asyncio.sleep(wait_first)

                while True:
                    try:
                        await func()
                    except asyncio.CancelledError:
                        break
                    except Exception as exc:
                        await _handle_exc(exc, on_exception)

                    await asyncio.sleep(seconds)

                if on_complete:
                    await on_complete()

            asyncio.ensure_future(loop())

        return wrapped

    return decorator


class AtomicMutableValue(Generic[T]):
    def __init__(self, value: T):
        self.value = value
        self._lock = asyncio.Lock()
        self._subscribers: list[asyncio.Event] = []

    async def get(self) -> T:
        async with self._lock:
            return self.value

    async def set(self, value: T):
        async with self._lock:
            self.value = value

        for event in self._subscribers:
            event.set()

    def subscribe(self) -> asyncio.Event:
        event = asyncio.Event()
        self._subscribers.append(event)
        return event

    def unsubscribe(self, event: asyncio.Event):
        self._subscribers.remove(event)

    def get_subscribers_count(self) -> int:
        return len(self._subscribers)


class Timer:
    def __init__(
        self,
        unit: Literal["s", "ms"] = "ms",
        rounded: bool = False,
    ):
        self.unit = unit
        self.rounded = rounded

    def start(self):
        self._start = time.perf_counter()

    def end(self):
        cost = time.perf_counter() - self._start
        if self.unit == "ms":
            self.duration = cost * 1000
        else:
            self.duration = cost

        if self.rounded:
            self.duration = round(self.duration)

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *args, **kwargs):
        self.end()

    async def __aenter__(self):
        self.start()
        return self

    async def __aexit__(self, *args, **kwargs):
        self.end()


@dataclass
class Stats:
    count: int = 0
    total_cost_ms: float = 0
    avg_cost_ms: float = 0
    max: float = 0
    min: float = 0

    def reset(self):
        self.count = 0
        self.total_cost_ms = 0
        self.avg_cost_ms = 0
        self.max = 0
        self.min = 0


class StatsTracker:
    def __init__(self, period_seconds: int):
        self.period_seconds = period_seconds
        self.__since: float | None = None
        self.__stats: Stats = Stats()

    def set_period_seconds(self, period_seconds: int):
        self.period_seconds = period_seconds
        self.reset()

    def collect(self, count: int = 1, cost_ms: float = 0):
        if self.__since is None:
            self.__since = time.monotonic()

        self.__stats.count += count
        self.__stats.total_cost_ms += cost_ms
        self.__stats.max = max(self.__stats.max, cost_ms)
        self.__stats.min = min(self.__stats.min, cost_ms)

    def get_stats(self) -> dict[str, Any]:
        if self.__stats.count > 0:
            self.__stats.avg_cost_ms = round(self.__stats.total_cost_ms / self.__stats.count, 1)

        self.__stats.total_cost_ms = round(self.__stats.total_cost_ms, 1)
        self.__stats.min = round(self.__stats.min, 1)
        self.__stats.max = round(self.__stats.max, 1)
        return asdict(self.__stats)

    def reset(self):
        self.__since = None
        self.__stats.reset()
