import dataclasses
import importlib
from typing import Type, Optional
from loguru import logger

from loglite.harvesters.base import Harvester


def import_class(fully_qualified_name: str) -> Optional[Type[Harvester]]:
    module_path, class_name = fully_qualified_name.rsplit(".", 1)

    try:
        module = importlib.import_module(module_path)
        return getattr(module, class_name)
    except (ModuleNotFoundError, ImportError, AttributeError):
        logger.error(f"Failed to import harvester class: {fully_qualified_name}")
        return None


class HarvesterManager:
    def __init__(self):
        self.harvesters: dict[str, Harvester] = {}

    def load_harvesters(self, configs: list[dict]):
        for config in configs:
            type_ = config.get("type")
            name = config.get("name", type_)
            config_data = config.get("config", {})

            HarvesterClass = import_class(type_)
            if not HarvesterClass:
                continue

            ConfigClass = HarvesterClass.get_config_type()
            if not ConfigClass:
                logger.warning(
                    f"Harvester {type_} does not define a valid get_config_class method. Ignoring..."
                )
                continue

            try:
                # Dataclasses don't accept extra arguments, so we must filter the config dict
                # to only include fields defined in the dataclass.
                field_names = {f.name for f in dataclasses.fields(ConfigClass)}
                config_obj = ConfigClass(
                    **{k: v for k, v in config_data.items() if k in field_names}
                )
                self.harvesters[name] = HarvesterClass(name, config_obj)
            except Exception as e:
                logger.exception(f"Failed to initialize harvester {name} ({type_}): {e}")

    async def start_all(self):
        for name, harvester in self.harvesters.items():
            logger.info(f"Starting harvester: {name}")
            await harvester.start()

    async def stop_all(self):
        for name, harvester in self.harvesters.items():
            logger.info(f"Stopping harvester: {name}")
            await harvester.stop()
