from __future__ import annotations
import asyncio
from typing import Generic, TypeVar

T = TypeVar("T")


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


LAST_INSERT_LOG_ID = AtomicMutableValue[int]()
