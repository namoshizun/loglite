import asyncio
from typing import Type
from loguru import logger
from loglite.harvesters.base import Harvester
from loglite.globals import BACKLOG

class HarvesterManager:
    def __init__(self):
        self.harvesters: dict[str, Harvester] = {}
        self._registry: dict[str, Type[Harvester]] = {}

    def register(self, type_name: str, harvester_cls: Type[Harvester]):
        self._registry[type_name] = harvester_cls

    def load_harvesters(self, configs: list[dict]):
        for config in configs:
            type_name = config.get("type")
            name = config.get("name", type_name)
            if not type_name or type_name not in self._registry:
                logger.warning(f"Unknown harvester type: {type_name}")
                continue
            
            harvester_cls = self._registry[type_name]
            harvester = harvester_cls(name, config)
            self.harvesters[name] = harvester

    async def start_all(self):
        for name, harvester in self.harvesters.items():
            logger.info(f"Starting harvester: {name}")
            await harvester.start()

    async def stop_all(self):
        for name, harvester in self.harvesters.items():
            logger.info(f"Stopping harvester: {name}")
            await harvester.stop()

    async def ingest(self, log: dict):
        await BACKLOG.add(log)
