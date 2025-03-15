from __future__ import annotations
import dataclasses
import yaml
from typing import Any
from pathlib import Path
from dataclasses import dataclass, field

from .types import Migration


@dataclass
class Config:
    migrations: list[Migration]
    host: str = "127.0.0.1"
    port: int = 7788
    log_table_name: str = "Log"
    sqlite_dir: Path = Path("./db")
    sqlite_params: dict[str, Any] = field(default_factory=dict)
    allow_origin: str = "*"
    debug: bool = False
    db_path: Path = field(init=False)
    sse_limit: int = 1000
    sse_debounce_ms: int = 500
    task_diagnostics_interval: int = 60  # seconds

    def __post_init__(self):
        if isinstance(self.sqlite_dir, str):
            self.sqlite_dir = Path(self.sqlite_dir)

        self.sqlite_dir.mkdir(parents=True, exist_ok=True)
        self.db_path = self.sqlite_dir / "logs.db"

    @classmethod
    def from_file(cls, config_path: str | Path):
        if isinstance(config_path, str):
            config_path = Path(config_path)

        if not config_path.exists():
            raise FileNotFoundError(f"Config file not found: {config_path}")

        with config_path.open("r") as f:
            config = yaml.safe_load(f)

        args = {}

        for field in dataclasses.fields(cls):
            if field.name in config:
                args[field.name] = config[field.name]
                continue

            is_required = (
                field.default is dataclasses.MISSING
                and field.default_factory is dataclasses.MISSING
                and field.init
            )
            if is_required:
                raise ValueError(f"{field.name} is missing in config")

        return cls(**args)
