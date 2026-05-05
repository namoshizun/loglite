import asyncio
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Any, Generic, Optional, TypeVar, get_args

from loglite import _core


@dataclass
class BaseHarvesterConfig:
    """Base configuration for all harvesters."""

    pass


T = TypeVar("T", bound=BaseHarvesterConfig)


def _get_param_type(cls: type["Harvester"]) -> Optional[type]:
    for base in getattr(cls, "__orig_bases__", []):
        origin = getattr(base, "__origin__", None)
        if origin and issubclass(origin, Harvester):
            args = get_args(base)
            if args:
                return args[0]


class Harvester(ABC, Generic[T]):
    def __init__(self, name: str, config: T):
        self.name = name
        self.config = config
        self._running = False
        self._task: asyncio.Task | None = None

    def __init_subclass__(cls, **kwargs: Any):
        super().__init_subclass__(**kwargs)
        if _type := _get_param_type(cls):
            if not issubclass(_type, BaseHarvesterConfig):
                raise TypeError(
                    f"'{cls.__name__}' must inherit from Harvester[T] where T is a subclass of "
                    f"BaseHarvesterConfig.  Example: class {cls.__name__}(Harvester[MyConfig]):"
                )

    @classmethod
    def get_config_type(cls) -> type[T]:
        """Extracts the concrete type of T from the class hierarchy."""
        if _type := _get_param_type(cls):
            if issubclass(_type, BaseHarvesterConfig):
                return _type  # pyright: ignore[reportReturnType]
        return BaseHarvesterConfig  # pyright: ignore[reportReturnType]

    @abstractmethod
    async def run(self):
        raise NotImplementedError

    async def start(self):
        if self._running:
            return
        self._running = True
        self._task = asyncio.create_task(self.run())

    async def stop(self):
        if not self._running:
            return
        self._running = False
        if not self._task:
            return
        self._task.cancel()
        try:
            await self._task
        except asyncio.CancelledError:
            pass
        self._task = None

    def ingest(self, log: dict[str, Any]):
        _core.push_to_backlog(log)
