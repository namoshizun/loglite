import pytest

from loglite.harvesters.manager import HarvesterManager
from loglite.harvesters.socket import SocketHarvesterConfig
from loglite.harvesters.zmq import ZMQHarvesterConfig


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


def test_manager_silently_skips_bad_or_missing_types():
    manager = HarvesterManager()
    manager.load_harvesters(
        [
            {"type": "nothing.here", "name": "bad_file"},
            {},  # missing type
        ]
    )
    assert len(manager.harvesters) == 0
