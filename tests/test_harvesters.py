from typing import Type
import pytest
import asyncio
import json
import os
from loglite.harvesters.base import BaseHarvesterConfig, Harvester
from loglite.harvesters.manager import HarvesterManager
from loglite.harvesters.file import FileHarvester

from loglite.harvesters.socket import SocketHarvester
from loglite.harvesters.file import FileHarvesterConfig
from loglite.harvesters.socket import SocketHarvesterConfig
from loglite.globals import BACKLOG
from loglite.backlog import Backlog


class MockHarvester(Harvester):
    async def run(self):
        while self._running:
            await asyncio.sleep(0.1)

    @classmethod
    def get_config_class(cls) -> Type[BaseHarvesterConfig]:
        return BaseHarvesterConfig


@pytest.fixture
def manager():
    return HarvesterManager()


@pytest.fixture
def backlog():
    b = Backlog(100)
    BACKLOG.set(b)
    return b


@pytest.mark.asyncio
async def test_harvester_lifecycle(manager: HarvesterManager):
    manager.load_harvesters(
        [{"type": "tests.test_harvesters.MockHarvester", "name": "mock harvester"}]
    )

    assert "mock harvester" in manager.harvesters
    harvester = manager.harvesters["mock harvester"]

    await manager.start_all()
    assert harvester._running
    assert harvester._task is not None

    await manager.stop_all()
    assert not harvester._running
    assert harvester._task is None


@pytest.mark.asyncio
async def test_file_harvester(tmp_path, backlog):
    log_file = tmp_path / "test.log"
    log_file.touch()

    harvester = FileHarvester("file_test", FileHarvesterConfig(path=str(log_file)))
    await harvester.start()

    # Wait for harvester to be ready
    await asyncio.sleep(0.1)

    # Write log
    log_entry = {"message": "test log", "timestamp": "2023-01-01T00:00:00"}
    with open(log_file, "a") as f:
        f.write(json.dumps(log_entry) + "\n")
        f.flush()
        os.fsync(f.fileno())

    # Wait for processing
    await asyncio.sleep(0.5)

    await harvester.stop()

    assert len(backlog.value) == 1
    assert backlog.value[0]["message"] == "test log"


@pytest.mark.asyncio
async def test_socket_harvester(backlog):
    port = 9999
    harvester = SocketHarvester("socket_test", SocketHarvesterConfig(port=port, host="127.0.0.1"))
    await harvester.start()

    # Wait for server to start
    await asyncio.sleep(0.5)

    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    log_entry = {"message": "socket log", "timestamp": "2023-01-01T00:00:00"}
    writer.write((json.dumps(log_entry) + "\n").encode())
    await writer.drain()
    writer.close()
    await writer.wait_closed()

    # Wait for processing
    await asyncio.sleep(0.5)

    await harvester.stop()

    assert len(backlog.value) == 1
    assert backlog.value[0]["message"] == "socket log"
