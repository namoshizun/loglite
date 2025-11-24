import asyncio
from abc import ABC, abstractmethod
from typing import Any, Type
from loglite.globals import BACKLOG
from loglite.harvesters.config import BaseHarvesterConfig


class Harvester(ABC):
    CONFIG_CLASS: Type[BaseHarvesterConfig] = BaseHarvesterConfig

    def __init__(self, name: str, config: BaseHarvesterConfig):
        self.name = name
        if not isinstance(config, BaseHarvesterConfig):
            raise TypeError(
                f"Config must be an instance of BaseHarvesterConfig, got {type(config)}"
            )
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
