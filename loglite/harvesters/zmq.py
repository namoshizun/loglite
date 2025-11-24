import asyncio
import zmq
from datetime import datetime
from loguru import logger
from loglite.harvesters.base import Harvester


class ZMQHarvester(Harvester):
    def __init__(self, name: str, config: dict):
        super().__init__(name, config)
        self.context = zmq.asyncio.Context()
        self.socket = None

    async def run(self):
        endpoint = self.config.get("endpoint")
        if not endpoint:
            logger.error(f"ZMQHarvester {self.name}: 'endpoint' is required")
            return

        socket_type_str = self.config.get("socket_type", "PULL").upper()
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
