import asyncio
import os
import json
from datetime import datetime
from loguru import logger
from loglite.harvesters.base import Harvester


class FileHarvester(Harvester):
    def __init__(self, name: str, config: dict):
        super().__init__(name, config)
        self.fd = None

    async def run(self):
        path = self.config.get("path")
        if not path:
            logger.error(f"FileHarvester {self.name}: 'path' is required")
            return

        if not os.path.exists(path):
            logger.warning(f"FileHarvester {self.name}: file {path} does not exist, waiting...")
            while not os.path.exists(path):
                await asyncio.sleep(1)
                if not self._running:
                    return

        logger.info(f"FileHarvester {self.name}: tailing {path}")
        try:
            self.fd = os.open(path, os.O_RDONLY)
            os.lseek(self.fd, 0, os.SEEK_END)
            buffer = b""

            while self._running:
                try:
                    chunk = os.read(self.fd, 4096)
                except OSError as e:
                    logger.error(f"FileHarvester {self.name}: error reading file: {e}")
                    break

                if not chunk:
                    await asyncio.sleep(0.1)
                    continue

                buffer += chunk
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    if not line:
                        continue

                    try:
                        line_str = line.decode("utf-8")
                        log_entry = json.loads(line_str)
                        # Ensure timestamp exists
                        if "timestamp" not in log_entry:
                            log_entry["timestamp"] = datetime.utcnow().isoformat()

                        await self.ingest(log_entry)
                    except json.JSONDecodeError:
                        logger.warning(f"FileHarvester {self.name}: failed to decode line: {line}")
                    except Exception as e:
                        logger.error(f"FileHarvester {self.name}: error processing line: {e}")

        except Exception as e:
            logger.error(f"FileHarvester {self.name}: error: {e}")
        finally:
            if self.fd:
                try:
                    os.close(self.fd)
                except OSError:
                    pass
                self.fd = None

    async def stop(self):
        await super().stop()
        # fd closing is handled in run finally block
