import yaml
from pathlib import Path
from dataclasses import dataclass, field

from .types import Migration


@dataclass
class Config:
    host: str = "127.0.0.1"
    port: int = 7788
    log_table_name: str = "Log"
    db_dir: Path = Path("./db")
    allow_origin: str = "*"
    debug: bool = False
    db_path: Path = field(init=False)
    migrations: list[Migration] = field(default_factory=list)

    def __post_init__(self):
        self.db_dir.mkdir(parents=True, exist_ok=True)
        self.db_path = self.db_dir / "logs.db"

    @classmethod
    def from_file(cls, config_path: str | Path):
        if isinstance(config_path, str):
            config_path = Path(config_path)

        if not config_path.exists():
            raise FileNotFoundError(f"Config file not found: {config_path}")

        with config_path.open("r") as f:
            config = yaml.safe_load(f)

        args = {}
        if "host" in config:
            args["host"] = config["host"]

        if "port" in config:
            args["port"] = config["port"]

        if "log_table_name" in config:
            args["log_table_name"] = config["log_table_name"]

        if "db_dir" in config:
            args["db_dir"] = Path(config["db_dir"])

        if "allow_origin" in config:
            args["allow_origin"] = config["allow_origin"]

        if "debug" in config:
            args["debug"] = config["debug"]

        if "migrations" not in config:
            raise ValueError("migrations is required")

        args["migrations"] = [
            Migration(
                version=migration["version"],
                rollout=migration["rollout"],
                rollback=migration["rollback"],
            )
            for migration in config["migrations"]
        ]

        return cls(**args)
