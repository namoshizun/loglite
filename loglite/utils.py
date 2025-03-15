from __future__ import annotations
import time
import asyncio
from dataclasses import dataclass, asdict
from functools import wraps
from typing import Any, Generic, Literal, TypeVar

T = TypeVar("T")


def repeat_every(
    *,
    seconds: float,
    wait_first: float | None = None,
    on_complete=None,
    on_exception=None,
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

    def __init__(self, value: T | None = None):
        self.value = value
        self.__lock = asyncio.Lock()
        self.__subscribers: list[asyncio.Event] = []

    async def get(self) -> T | None:
        async with self.__lock:
            return self.value

    async def set(self, value: T | None):
        async with self.__lock:
            self.value = value

        for event in self.__subscribers:
            event.set()

    def subscribe(self) -> asyncio.Event:
        event = asyncio.Event()
        self.__subscribers.append(event)
        return event

    def unsubscribe(self, event: asyncio.Event):
        self.__subscribers.remove(event)

    def get_subscribers_count(self) -> int:
        return len(self.__subscribers)


class Timer:
    def __init__(
        self,
        unit: Literal["s", "ms"] = "ms",
        rounded: bool = False,
    ):
        self.unit = unit
        self.rounded = rounded

    def __enter__(self):
        self.start = time.perf_counter()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        cost = time.perf_counter() - self.start
        if self.unit == "ms":
            self.duration = cost * 1000
        else:
            self.duration = cost

        if self.rounded:
            self.duration = round(self.duration)


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

    def collect(self, cost_ms: float):
        if self.__since is None:
            self.__since = time.monotonic()

        self.__stats.count += 1
        self.__stats.total_cost_ms += cost_ms
        self.__stats.max = max(self.__stats.max, cost_ms)
        self.__stats.min = min(self.__stats.min, cost_ms)

    def get_stats(self) -> dict[str, Any]:
        if self.__stats.count > 0:
            self.__stats.avg_cost_ms = round(
                self.__stats.total_cost_ms / self.__stats.count, 1
            )

        self.__stats.total_cost_ms = round(self.__stats.total_cost_ms, 1)
        self.__stats.min = round(self.__stats.min, 1)
        self.__stats.max = round(self.__stats.max, 1)
        return asdict(self.__stats)

    def reset(self):
        self.__since = None
        self.__stats.reset()
