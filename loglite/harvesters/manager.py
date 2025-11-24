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
            try:
                config_cls = getattr(harvester_cls, "CONFIG_CLASS", None)
                if config_cls:
                    # Dataclasses don't accept extra arguments, so we must filter the config dict
                    # to only include fields defined in the dataclass.
                    # Pydantic (v2) ignores extras by default, but dataclasses raise TypeError.
                    import dataclasses

                    if dataclasses.is_dataclass(config_cls):
                        field_names = {f.name for f in dataclasses.fields(config_cls)}
                        filtered_config = {k: v for k, v in config.items() if k in field_names}
                        config_obj = config_cls(**filtered_config)
                    else:
                        # Fallback for non-dataclass config classes (if any remain)
                        config_obj = config_cls(**config)
                else:
                    # Fallback or error if no config class defined (though we enforced it in base)
                    # For now, let's assume it might be a dict if some legacy harvester exists,
                    # but our base class forbids it. So we must have a config class.
                    # However, to be safe against custom harvesters not inheriting correctly:
                    from loglite.harvesters.config import BaseHarvesterConfig

                    if not isinstance(config, BaseHarvesterConfig):
                        # Try to use BaseHarvesterConfig if nothing else, but that won't have fields.
                        # Better to raise error if CONFIG_CLASS is missing but config is dict.
                        raise ValueError(f"Harvester {type_name} does not define CONFIG_CLASS")
                    config_obj = config

                harvester = harvester_cls(name, config_obj)
                self.harvesters[name] = harvester
            except Exception as e:
                logger.error(f"Failed to initialize harvester {name} ({type_name}): {e}")

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
