from __future__ import annotations
import asyncio
from loguru import logger
from collections import deque

from loglite.utils import AtomicMutableValue


class Backlog(AtomicMutableValue[deque[dict]]):
    def __init__(self, max_size: int):
        super().__init__(value=deque(maxlen=max_size))
        self._full_signal = None

    @property
    def full_signal(self) -> asyncio.Event:
        if evt := self._full_signal:
            return evt

        self._full_signal = asyncio.Event()
        return self._full_signal

    async def add(self, log: dict | list[dict]):
        async with self._lock:
            if isinstance(log, list):
                self.value.extend(log)
            else:
                self.value.append(log)

            if len(self.value) >= (self.value.maxlen or 0):
                logger.warning("backlog is full...")
                self.full_signal.set()

    async def flush(self) -> tuple[dict, ...]:
        async with self._lock:
            copy = tuple(self.value)
            self.value.clear()
            self.full_signal.clear()
            return copy
