from __future__ import annotations

import dataclasses
import os
import re
from contextlib import suppress
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml
from dotenv import load_dotenv

from loglite.types import CompressionConfig, Migration
from loglite.utils import convert_size_to_bytes

load_dotenv()

SIZE_RE = re.compile(r"^(\d+)([KMGT]B)$")
CONFIG_ENV_PREFIX = "LOGLITE_"


def _read_args_from_env() -> dict[str, Any]:
    args = {}
    for name, value in os.environ.items():
        if name.startswith(CONFIG_ENV_PREFIX):
            name = name[len(CONFIG_ENV_PREFIX) :]
            args[name.lower()] = value
    return args


@dataclass
class Config:
    migrations: list[Migration]
    auto_rollout: bool = True
    host: str = "127.0.0.1"
    port: int = 7788
    log_table_name: str = "Log"
    log_timestamp_field: str = "timestamp"
    sqlite_dir: Path = Path("./db")
    sqlite_params: dict[str, Any] = field(default_factory=dict)
    vacuum_max_days: int = 365 * 10
    vacuum_max_size: str = "1TB"  # pattern: \d+[KMGT]B
    vacuum_max_size_bytes: int = field(init=False)
    vacuum_target_size: str = "800GB"  # pattern: \d+[KMGT]B
    vacuum_target_size_bytes: int = field(init=False)
    vacuum_delete_batch_size: int = 2500
    allow_origin: str = "*"
    debug: bool = False
    db_path: Path = field(init=False)
    sse_limit: int = 1000
    sse_debounce_ms: int = 500
    backlog_max_size: int = 100
    compression: CompressionConfig = field(
        default_factory=lambda: {
            "enabled": False,
            "columns": [],
        }
    )
    task_diagnostics_interval: int = 60  # seconds
    task_backlog_flush_interval: int = 5  # seconds
    task_backlog_max_size: int = 200  # max logs in backlog before triggering force flush
    task_vacuum_interval: int = 60 * 2  # 2 minutes
    task_vacuum_max_size: int = 20  # MB
    harvesters: list[dict[str, Any]] = field(default_factory=list)

    def __post_init__(self):
        # Run validations
        for name, _ in self.__dataclass_fields__.items():
            if method := getattr(self, f"validate_{name}", None):
                setattr(self, name, method(getattr(self, name)))

        # Misc
        if isinstance(self.sqlite_dir, str):
            self.sqlite_dir = Path(self.sqlite_dir)

        self.sqlite_dir.mkdir(parents=True, exist_ok=True)
        self.db_path = self.sqlite_dir / "logs.db"

    def validate_vacuum_max_size(self, v: str):
        if not SIZE_RE.match(v):
            raise ValueError(f"Invalid vacuum_max_size: {v}")
        value, unit = SIZE_RE.findall(v)[0]
        self.vacuum_max_size_bytes = convert_size_to_bytes(int(value), unit)
        return v

    def validate_vacuum_target_size(self, v: str):
        if not SIZE_RE.match(v):
            raise ValueError(f"Invalid vacuum_target_size: {v}")
        value, unit = SIZE_RE.findall(v)[0]
        self.vacuum_target_size_bytes = convert_size_to_bytes(int(value), unit)
        return v

    @classmethod
    def from_file(cls, config_path: str | Path):
        if isinstance(config_path, str):
            config_path = Path(config_path)

        if not config_path.exists():
            raise FileNotFoundError(f"Config file not found: {config_path}")

        with config_path.open("r") as f:
            config_data = yaml.safe_load(f)

        cfg_args = {}
        env_args = _read_args_from_env()

        # Validate that required fields are present
        for _field in dataclasses.fields(cls):
            # Read from environment variables
            if _field.name in env_args:
                with suppress(Exception):
                    field_type_class = eval(_field.type)  # pyright: ignore[reportArgumentType]
                    cfg_args[_field.name] = field_type_class(env_args[_field.name])
                    continue

            # Read from config file data
            if _field.name in config_data:
                cfg_args[_field.name] = config_data[_field.name]
                continue

            # No value provided, ensure it is an optional field
            is_required = (
                _field.default is dataclasses.MISSING
                and _field.default_factory is dataclasses.MISSING
                and _field.init
            )
            if is_required:
                raise ValueError(f"{_field.name} is missing in config")

        return cls(**cfg_args)
