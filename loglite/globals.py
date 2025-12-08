from __future__ import annotations
import asyncio
from contextlib import suppress
from typing import Any, TypeVar, Generic, Optional
from loglite.backlog import Backlog
from loglite.utils import AtomicMutableValue, StatsTracker


T = TypeVar("T")


class ObjectProxy(Generic[T]):
    def __init__(self):
        self._instance: Optional[T] = None

    def instance(self) -> T:
        if self._instance is None:
            raise RuntimeError(
                f"{self.__class__.__name__} has not been initialized. Call set() first."
            )
        return self._instance

    def set(self, instance: T) -> None:
        self._instance = instance

    def __getattr__(self, item: str) -> Any:
        return getattr(self.instance(), item)

    def reset(self) -> None:
        """Reset the singleton instance (mainly for testing)"""
        self._instance = None


with suppress(RuntimeError):
    OPERATION_LOCK = asyncio.Lock()
    LAST_INSERT_LOG_ID = AtomicMutableValue[int](0)

INGESTION_STATS = StatsTracker(period_seconds=10)

QUERY_STATS = StatsTracker(period_seconds=10)

BACKLOG = ObjectProxy[Backlog]()
