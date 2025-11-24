import asyncio
from abc import ABC, abstractmethod
from typing import Any
from loglite.globals import BACKLOG


class Harvester(ABC):
    def __init__(self, name: str, config: dict[str, Any]):
        self.name = name
        self.config = config
        self._running = False
        self._task: asyncio.Task | None = None

    @abstractmethod
    async def run(self):
        """Main loop of the harvester."""
        pass

    async def start(self):
        if self._running:
            return
        self._running = True
        self._task = asyncio.create_task(self.run())

    async def stop(self):
        if not self._running:
            return
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None

    async def ingest(self, log: dict[str, Any]):
        await BACKLOG.instance().add(log)
