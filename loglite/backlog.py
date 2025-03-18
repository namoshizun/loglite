import sys
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

        if sys.version_info >= (3, 10):
            self._full_signal = asyncio.Event()
        else:
            self._full_signal = asyncio.Event(loop=asyncio.get_running_loop())
        return self._full_signal

    def set_maxlen(self, max_size: int):
        del self.value
        self.value = deque(maxlen=max_size)

    async def add(self, log: dict):
        async with self._lock:
            self.value.append(log)

            if len(self.value) == self.value.maxlen:
                logger.warning("backlog is full...")
                self.full_signal.set()

    async def flush(self) -> tuple[dict, ...]:
        async with self._lock:
            copy = tuple(self.value)
            self.value.clear()
            self.full_signal.clear()
            return copy
