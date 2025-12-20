from __future__ import annotations
import asyncio
import json
from datetime import datetime
from dataclasses import dataclass
from loguru import logger
from loglite.harvesters.base import Harvester, BaseHarvesterConfig


@dataclass
class SocketHarvesterConfig(BaseHarvesterConfig):
    """Configuration for SocketHarvester."""

    host: str = "0.0.0.0"
    port: int | None = None
    path: str | None = None

    def __post_init__(self):
        if not self.port and not self.path:
            raise ValueError("Either 'port' or 'path' must be provided")


class SocketHarvester(Harvester[SocketHarvesterConfig]):
    def __init__(self, name: str, config: SocketHarvesterConfig):
        super().__init__(name, config)
        self.server = None

    async def handle_client(self, reader, writer):
        addr = writer.get_extra_info("peername")
        logger.debug(f"SocketHarvester {self.name}: new connection from {addr}")

        buffer = b""
        while self._running:
            try:
                data = await reader.read(4096)
                if not data:
                    break

                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    if not line:
                        continue

                    try:
                        log_entry = json.loads(line.decode("utf-8"))
                        # Ensure timestamp exists
                        if "timestamp" not in log_entry:
                            log_entry["timestamp"] = datetime.utcnow().isoformat()

                        await self.ingest(log_entry)
                    except json.JSONDecodeError:
                        logger.warning(f"SocketHarvester {self.name}: failed to decode line")
                    except Exception as e:
                        logger.error(f"SocketHarvester {self.name}: error processing line: {e}")

            except Exception as e:
                logger.error(f"SocketHarvester {self.name}: connection error: {e}")
                break

        logger.debug(f"SocketHarvester {self.name}: connection closed from {addr}")
        writer.close()
        await writer.wait_closed()

    async def run(self):
        host = self.config.host
        port = self.config.port
        path = self.config.path

        # validation handled by model

        try:
            if path:
                self.server = await asyncio.start_unix_server(self.handle_client, path=path)
                logger.info(f"SocketHarvester {self.name}: listening on unix socket {path}")
            else:
                self.server = await asyncio.start_server(self.handle_client, host, port)
                logger.info(f"SocketHarvester {self.name}: listening on {host}:{port}")

            async with self.server:
                await self.server.serve_forever()
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.error(f"SocketHarvester {self.name}: failed to start server: {e}")

    async def stop(self):
        await super().stop()
        if self.server:
            self.server.close()
            await self.server.wait_closed()
