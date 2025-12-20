import asyncio
import zmq
from datetime import datetime
from dataclasses import dataclass
from typing import Literal, Type, get_args
from loguru import logger

from loglite.harvesters.base import Harvester, BaseHarvesterConfig


ZMQHarvesterSocketType = Literal["PULL", "SUB"]


@dataclass
class ZMQHarvesterConfig(BaseHarvesterConfig):
    """Configuration for ZMQHarvester."""

    endpoint: str
    socket_type: ZMQHarvesterSocketType = "PULL"
    bind: bool = False

    def __post_init__(self):
        if not self.endpoint:
            raise ValueError("'endpoint' is required")
        if self.socket_type not in get_args(ZMQHarvesterSocketType):
            raise ValueError(f"Invalid socket_type: {self.socket_type}")


class ZMQHarvester(Harvester[ZMQHarvesterConfig]):
    def __init__(self, name: str, config: ZMQHarvesterConfig):
        super().__init__(name, config)
        self.context = zmq.asyncio.Context()
        self.socket = None

    @classmethod
    def get_config_class(cls) -> Type[BaseHarvesterConfig]:
        return ZMQHarvesterConfig

    async def run(self):
        endpoint = self.config.endpoint
        # endpoint is required by model

        socket_type_str = self.config.socket_type
        socket_type = getattr(zmq, socket_type_str, zmq.PULL)

        self.socket = self.context.socket(socket_type)

        try:
            if self.config.get("bind", False):
                self.socket.bind(endpoint)
                logger.info(f"ZMQHarvester {self.name}: bound to {endpoint}")
            else:
                self.socket.connect(endpoint)
                logger.info(f"ZMQHarvester {self.name}: connected to {endpoint}")
        except Exception as e:
            logger.error(f"ZMQHarvester {self.name}: failed to setup socket: {e}")
            return

        while self._running:
            try:
                if await self.socket.poll(timeout=1000):
                    msg = await self.socket.recv_json()

                    # Ensure timestamp exists
                    if "timestamp" not in msg:
                        msg["timestamp"] = datetime.utcnow().isoformat()

                    await self.ingest(msg)
                else:
                    # Yield control if no message
                    await asyncio.sleep(0.1)
            except Exception as e:
                logger.error(f"ZMQHarvester {self.name}: error receiving message: {e}")
                await asyncio.sleep(1)

    async def stop(self):
        await super().stop()
        if self.socket:
            self.socket.close()
        self.context.term()
