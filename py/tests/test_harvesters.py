import asyncio
import json
from pathlib import Path
from typing import Any

import pytest

from loglite.harvesters.base import BaseHarvesterConfig, Harvester
from loglite.harvesters.manager import HarvesterManager, import_class
from loglite.harvesters.socket import SocketHarvester, SocketHarvesterConfig
from loglite.harvesters.zmq import ZMQHarvester, ZMQHarvesterConfig


class MockHarvester(Harvester):
    async def run(self):
        while self._running:
            await asyncio.sleep(0.1)


class InvalidConfigClassHarvester(Harvester):
    @classmethod
    def get_config_type(cls):
        return None

    async def run(self):
        raise NotImplementedError


@pytest.fixture
def manager():
    return HarvesterManager()


def test_invalid_harvester_type_argument():
    with pytest.raises(TypeError):

        class BadHarvester(Harvester[int]):  # pyright: ignore[reportInvalidTypeArguments]
            pass


def test_harvester_config_validation(tmp_path: Path):
    assert MockHarvester.get_config_type() is BaseHarvesterConfig

    with pytest.raises(ValueError, match="Either 'port' or 'path' must be provided"):
        SocketHarvesterConfig()

    with pytest.raises(ValueError, match="'endpoint' is required"):
        ZMQHarvesterConfig(endpoint="")

    with pytest.raises(ValueError, match="Invalid socket_type"):
        ZMQHarvesterConfig(endpoint="inproc://logs", socket_type="PUB")  # pyright: ignore[reportArgumentType]


def test_harvester_manager_ignores_invalid_configs(manager: HarvesterManager, tmp_path: Path):
    assert import_class("tests.test_harvesters.NoSuchHarvester") is None

    manager.load_harvesters(
        [
            {},
            {"type": "tests.test_harvesters.NoSuchHarvester"},
            {"type": "tests.test_harvesters.InvalidConfigClassHarvester", "name": "bad config"},
        ]
    )

    assert list(manager.harvesters) == []


def test_harvester_manager_loads_python_harvesters(manager: HarvesterManager, tmp_path: Path):
    manager.load_harvesters([{"type": "tests.test_harvesters.MockHarvester", "name": "mock"}])
    assert "mock" in manager.harvesters
    assert isinstance(manager.harvesters["mock"], MockHarvester)


@pytest.mark.asyncio
async def test_harvester_lifecycle(manager: HarvesterManager):
    manager.load_harvesters(
        [{"type": "tests.test_harvesters.MockHarvester", "name": "mock harvester"}]
    )

    harvester = manager.harvesters["mock harvester"]

    await manager.start_all()
    assert harvester._running
    assert harvester._task is not None

    await manager.stop_all()
    assert not harvester._running
    assert harvester._task is None


@pytest.mark.asyncio
async def test_socket_harvester(captured_logs: list[dict]):
    port = 9999
    harvester = SocketHarvester("socket_test", SocketHarvesterConfig(port=port, host="127.0.0.1"))
    await harvester.start()
    await asyncio.sleep(0.5)

    _, writer = await asyncio.open_connection("127.0.0.1", port)
    log_entry = {"message": "socket log", "timestamp": "2023-01-01T00:00:00"}
    writer.write((json.dumps(log_entry) + "\n").encode())
    await writer.drain()
    writer.close()
    await writer.wait_closed()

    await asyncio.sleep(0.5)
    await harvester.stop()

    assert len(captured_logs) == 1
    assert captured_logs[0]["message"] == "socket log"


@pytest.mark.asyncio
async def test_socket_harvester_handles_empty_invalid_and_missing_timestamp_lines(
    captured_logs: list[dict],
):
    class Reader:
        def __init__(self):
            self.chunks = [
                b'\n{"message":"socket log without timestamp"}\n{',
                b"\n",
                b"",
            ]

        async def read(self, size: int) -> bytes:
            return self.chunks.pop(0)

    class Writer:
        def get_extra_info(self, name: str):
            return ("127.0.0.1", 12345)

        def close(self):
            pass

        async def wait_closed(self):
            pass

    harvester = SocketHarvester("socket_test", SocketHarvesterConfig(port=9998))
    harvester._running = True

    await harvester.handle_client(Reader(), Writer())  # pyright: ignore[reportArgumentType]

    assert len(captured_logs) == 1
    assert captured_logs[0]["message"] == "socket log without timestamp"
    assert "timestamp" in captured_logs[0]


@pytest.mark.asyncio
async def test_socket_harvester_stops_on_connection_error():
    class BrokenReader:
        async def read(self, size: int) -> bytes:
            raise RuntimeError("read failed")

    class Writer:
        def get_extra_info(self, name: str):
            return None

        def close(self):
            pass

        async def wait_closed(self):
            pass

    harvester = SocketHarvester("socket_test", SocketHarvesterConfig(port=9997))
    harvester._running = True

    await harvester.handle_client(BrokenReader(), Writer())  # pyright: ignore[reportArgumentType]


@pytest.mark.asyncio
async def test_zmq_harvester_receives_json_and_closes(
    monkeypatch: pytest.MonkeyPatch, captured_logs: list[dict]
):
    class Socket:
        def __init__(self):
            self.connected_to: str | None = None
            self.closed = False

        def connect(self, endpoint: str):
            self.connected_to = endpoint

        async def poll(self, timeout: int) -> bool:
            return True

        async def recv_json(self) -> dict[str, str]:
            return {"message": "zmq log"}

        def close(self):
            self.closed = True

    class Context:
        def __init__(self):
            self.socket_instance = Socket()
            self.terminated = False

        def socket(self, socket_type: int) -> Socket:
            return self.socket_instance

        def term(self):
            self.terminated = True

    context = Context()
    monkeypatch.setattr("loglite.harvesters.zmq.zmq.asyncio.Context", lambda: context)
    harvester = ZMQHarvester("zmq_test", ZMQHarvesterConfig(endpoint="inproc://logs"))

    # Stop after receiving the first message.
    original_ingest = harvester.ingest

    def ingest_and_stop(log: dict[str, Any]):
        original_ingest(log)
        harvester._running = False

    harvester.ingest = ingest_and_stop  # pyright: ignore[reportAttributeAccessIssue]
    harvester._running = True

    await harvester.run()
    await harvester.stop()

    assert context.socket_instance.connected_to == "inproc://logs"
    assert len(captured_logs) == 1
    assert captured_logs[0]["message"] == "zmq log"
    assert "timestamp" in captured_logs[0]
    assert context.socket_instance.closed is True
    assert context.terminated is True


@pytest.mark.asyncio
async def test_zmq_harvester_returns_when_socket_setup_fails(monkeypatch: pytest.MonkeyPatch):
    class Socket:
        def bind(self, endpoint: str):
            raise RuntimeError("bind failed")

    class Context:
        def socket(self, socket_type: int) -> Socket:
            return Socket()

    monkeypatch.setattr("loglite.harvesters.zmq.zmq.asyncio.Context", Context)
    harvester = ZMQHarvester("zmq_test", ZMQHarvesterConfig(endpoint="inproc://logs", bind=True))
    harvester._running = True

    await harvester.run()

    assert harvester._running is True
