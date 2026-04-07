from pathlib import Path

import pytest

from loglite.harvesters.file import FileHarvesterConfig
from loglite.harvesters.manager import HarvesterManager
from loglite.harvesters.socket import SocketHarvesterConfig
from loglite.harvesters.zmq import ZMQHarvesterConfig


def test_file_harvester_config_validation():
    # Valid config
    config = FileHarvesterConfig(path=Path("/tmp/test.log"))
    assert str(config.path) == "/tmp/test.log"

    # Missing path
    # Dataclasses raise TypeError for missing arguments
    with pytest.raises(TypeError):
        FileHarvesterConfig()  # pyright: ignore[reportCallIssue]

    # Empty path (custom validation)
    with pytest.raises(ValueError):
        FileHarvesterConfig(path="")  # pyright: ignore[reportArgumentType]


def test_zmq_harvester_config_validation():
    # Valid config
    config = ZMQHarvesterConfig(endpoint="tcp://127.0.0.1:5555")
    assert config.endpoint == "tcp://127.0.0.1:5555"
    assert config.socket_type == "PULL"  # Default

    # Invalid socket type
    with pytest.raises(ValueError):
        ZMQHarvesterConfig(endpoint="tcp://...", socket_type="INVALID")  # pyright: ignore[reportArgumentType]


def test_socket_harvester_config_validation():
    # Valid config
    config = SocketHarvesterConfig(port=9000)
    assert config.port == 9000
    assert config.host == "0.0.0.0"

    # Missing port and path
    with pytest.raises(ValueError, match="Either 'port' or 'path' must be provided"):
        SocketHarvesterConfig()


def test_manager_validation_error():
    manager = HarvesterManager()

    # Invalid config passed to load_harvesters
    # It should log an error but not crash
    manager.load_harvesters([{"type": "nothing.here", "name": "bad_file"}])

    assert "bad_file" not in manager.harvesters
