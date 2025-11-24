import os
import orjson
import asyncio
import aiofiles
from loguru import logger
from datetime import datetime, timezone

from loglite.harvesters.base import Harvester
from loglite.harvesters.config import FileHarvesterConfig


class FileHarvester(Harvester):
    CONFIG_CLASS = FileHarvesterConfig

    def __init__(self, name: str, config: FileHarvesterConfig):
        super().__init__(name, config)
        self.config: FileHarvesterConfig = self.config  # Type hint
        self._current_inode = None
        self._offset = 0

    async def run(self):
        path = self.config.path
        buffer = b""

        if not os.path.exists(path):
            logger.warning(f"FileHarvester {self.name}: file {path} does not exist, waiting...")
            while not os.path.exists(path):
                await asyncio.sleep(1)
                if not self._running:
                    return

        logger.info(f"FileHarvester {self.name}: tailing {path}")

        # Initial setup: get inode and seek to end
        try:
            stat = os.stat(path)
            self._current_inode = stat.st_ino
            self._offset = stat.st_size
        except OSError:
            self._offset = 0

        while self._running:
            try:
                # Check for file existence
                if not os.path.exists(path):
                    await asyncio.sleep(0.1)
                    continue

                try:
                    stat = os.stat(path)
                except OSError:
                    await asyncio.sleep(0.1)
                    continue

                # Check for rotation or truncation
                if stat.st_ino != self._current_inode:
                    logger.info(
                        f"FileHarvester {self.name}: file rotated (inode changed), reopening..."
                    )
                    self._current_inode = stat.st_ino
                    self._offset = 0
                    buffer = b""
                elif stat.st_size < self._offset:
                    logger.warning(f"FileHarvester {self.name}: file truncated, resetting offset")
                    self._offset = 0
                    buffer = b""

                async with aiofiles.open(path, mode="rb") as f:
                    await f.seek(self._offset)

                    while self._running:
                        chunk = await f.read(8192)

                        if not chunk:
                            # EOF reached
                            self._offset = await f.tell()

                            # Check if we need to rotate or if file was truncated
                            try:
                                if not os.path.exists(path):
                                    break

                                stat = os.stat(path)
                                if stat.st_ino != self._current_inode:
                                    logger.info(
                                        f"FileHarvester {self.name}: file rotated (inode changed), reopening..."
                                    )
                                    break  # Break inner loop to reopen

                                if stat.st_size < self._offset:
                                    logger.warning(
                                        f"FileHarvester {self.name}: file truncated, resetting offset"
                                    )
                                    break  # Break inner loop to handle truncation
                            except OSError:
                                break

                            await asyncio.sleep(0.1)
                            continue

                        buffer += chunk
                        while b"\n" in buffer:
                            line, buffer = buffer.split(b"\n", 1)
                            if not line:
                                continue

                            await self._process_line(line)

                        self._offset = await f.tell()

            except Exception as e:
                logger.error(f"FileHarvester {self.name}: error: {e}")
                await asyncio.sleep(1)

    async def _process_line(self, line: bytes):
        try:
            # orjson.loads accepts bytes directly
            log_entry = orjson.loads(line)

            if "timestamp" not in log_entry:
                log_entry["timestamp"] = datetime.now(timezone.utc).isoformat()

            await self.ingest(log_entry)
        except orjson.JSONDecodeError:
            logger.warning(f"FileHarvester {self.name}: failed to decode line: {line!r}")
        except Exception as e:
            logger.error(f"FileHarvester {self.name}: error processing line: {e}")
