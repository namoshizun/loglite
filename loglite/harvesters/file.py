import orjson
import asyncio
import aiofiles
from pathlib import Path
from loguru import logger
from dataclasses import dataclass
from datetime import datetime, timezone

from loglite.harvesters.base import Harvester, BaseHarvesterConfig


@dataclass
class FileHarvesterConfig(BaseHarvesterConfig):
    """Configuration for FileHarvester."""

    path: Path

    def __post_init__(self):
        if not self.path:
            raise ValueError("'path' is required")

        if isinstance(self.path, str):
            self.path = Path(self.path)


class FileHarvester(Harvester[FileHarvesterConfig]):
    def __init__(self, name: str, config: FileHarvesterConfig):
        super().__init__(name, config)
        self._current_inode = None
        self._offset = 0

    async def _harvest_file(self, path: Path):
        buffer = b""
        async with aiofiles.open(path, mode="rb") as f:
            await f.seek(self._offset)

            while self._running:
                chunk = await f.read(8192)

                if not chunk:
                    # EOF reached, update offset
                    self._offset = await f.tell()

                    # Check if we need to rotate or if file was truncated
                    try:
                        if not path.exists():
                            return

                        stat = path.stat()
                        if stat.st_ino != self._current_inode:
                            logger.info(
                                f"FileHarvester {self.name}: file rotated (inode changed), reopening..."
                            )
                            return

                        if stat.st_size < self._offset:
                            logger.warning(
                                f"FileHarvester {self.name}: file truncated, resetting offset"
                            )
                            return
                    except OSError:
                        return

                    await asyncio.sleep(0.5)
                    continue

                buffer += chunk
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    if line:
                        await self._process_line(line)

                self._offset = await f.tell()

    async def run(self):
        path = self.config.path

        # Wait for the file to exist
        while not path.exists():
            await asyncio.sleep(5)
            if not self._running:
                return
            logger.opt(colors=True).info(
                f"<dim>FileHarvester {self.name}: file {path} does not exist, waiting...</dim>"
            )

        logger.info(f"FileHarvester {self.name}: tailing {path}")

        # Initial setup: get inode and seek to end
        try:
            stat = path.stat()
            self._current_inode = stat.st_ino
            self._offset = stat.st_size
        except OSError:
            self._offset = 0

        while self._running:
            try:
                # Check if the file still exists
                if not path.exists():
                    await asyncio.sleep(0.1)
                    continue

                try:
                    stat = path.stat()
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
                elif stat.st_size < self._offset:
                    logger.warning(f"FileHarvester {self.name}: file truncated, resetting offset")
                    self._offset = 0

                await self._harvest_file(path)

            except Exception as e:
                logger.error(f"FileHarvester {self.name}: error: {e}")
                await asyncio.sleep(1)

    async def _process_line(self, line: bytes):
        try:
            log_entry = orjson.loads(line)

            if "timestamp" not in log_entry:
                log_entry["timestamp"] = datetime.now(timezone.utc).isoformat()

            await self.ingest(log_entry)
        except orjson.JSONDecodeError:
            logger.warning(f"FileHarvester {self.name}: failed to decode line: {line!r}")
        except Exception as e:
            logger.error(f"FileHarvester {self.name}: error processing line: {e}")
