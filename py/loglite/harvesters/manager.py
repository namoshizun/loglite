import dataclasses
import importlib
from typing import Any, Optional, Type

from loguru import logger

from loglite.harvesters.base import Harvester

# Harvester types that are handled natively by the C++ core.
# The Python manager skips these so C++ doesn't double-start them.
_NATIVE_TYPES = {
    "FileHarvester",
    "loglite.harvesters.FileHarvester",
}


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

    def load_harvesters(self, configs: list[dict[str, Any]]) -> None:
        """Instantiate Python harvesters from a list of config dicts.

        Each dict must have a ``type`` key (fully-qualified class name) and
        optionally ``name`` and ``config`` (dict of string params).
        Native C++ harvester types are silently skipped.
        """
        for config in configs:
            type_ = config.get("type")
            if not type_:
                logger.warning("Harvester definition is missing 'type', skipping")
                continue

            if type_ in _NATIVE_TYPES:
                continue  # handled by C++ core

            name = config.get("name", type_)
            config_data: dict = config.get("config", {})

            harvester_class = import_class(type_)
            if harvester_class is None:
                continue

            config_class = harvester_class.get_config_type()
            if config_class is None:
                logger.warning(f"Harvester {type_} has no config type, skipping")
                continue

            field_names = {f.name for f in dataclasses.fields(config_class)}
            try:
                config_obj = config_class(
                    **{k: v for k, v in config_data.items() if k in field_names}
                )
                self.harvesters[name] = harvester_class(name, config_obj)  # pyright: ignore[reportArgumentType]
            except Exception:
                logger.exception(f"Failed to initialise harvester {name} ({type_})")

    async def start_all(self) -> None:
        for name, harvester in self.harvesters.items():
            logger.info(f"Starting harvester: {name}")
            await harvester.start()

    async def stop_all(self) -> None:
        for name, harvester in self.harvesters.items():
            logger.info(f"Stopping harvester: {name}")
            await harvester.stop()

    def __len__(self) -> int:
        return len(self.harvesters)
