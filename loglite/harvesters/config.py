from dataclasses import dataclass
from typing import Optional, Literal


@dataclass
class BaseHarvesterConfig:
    """Base configuration for all harvesters."""

    pass


@dataclass
class FileHarvesterConfig(BaseHarvesterConfig):
    """Configuration for FileHarvester."""

    path: str

    def __post_init__(self):
        if not self.path:
            raise ValueError("'path' is required")


@dataclass
class ZMQHarvesterConfig(BaseHarvesterConfig):
    """Configuration for ZMQHarvester."""

    endpoint: str
    socket_type: Literal["PULL", "SUB"] = "PULL"
    bind: bool = False

    def __post_init__(self):
        if not self.endpoint:
            raise ValueError("'endpoint' is required")
        if self.socket_type not in ("PULL", "SUB"):
            raise ValueError(f"Invalid socket_type: {self.socket_type}")


@dataclass
class SocketHarvesterConfig(BaseHarvesterConfig):
    """Configuration for SocketHarvester."""

    host: str = "0.0.0.0"
    port: Optional[int] = None
    path: Optional[str] = None

    def __post_init__(self):
        if not self.port and not self.path:
            raise ValueError("Either 'port' or 'path' must be provided")
