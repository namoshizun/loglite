# Loglite (Python)

The Python package is a thin convenience layer on top of the [loglite C++ core](../cpp). It provides:

- **CLI** (`loglite server run`, `loglite migrate rollout/rollback`) — delegates directly to the C++ server via a pybind11 extension (`_core`).
- **Built-in harvesters** — `FileHarvester`, `SocketHarvester`, and (optional) `ZMQHarvester`.
- **Custom harvesters** — subclass `loglite.Harvester[Config]` to write custom log ingestors in Python. Ingested entries are pushed directly into the C++ server's backlog in the same process.

The C++ core handles everything else: the HTTP server, SQLite read/write, migrations, SSE, and vacuuming. The Python package adds zero server-side overhead.

## Installation

Pre-built wheels are published to PyPI:

```bash
pip install loglite

# With ZeroMQ harvester support
pip install "loglite[zmq]"
```

## Writing a custom harvester

```python
from dataclasses import dataclass
from loglite.harvesters.base import BaseHarvesterConfig, Harvester

@dataclass
class MyConfig(BaseHarvesterConfig):
    source: str

class MyHarvester(Harvester[MyConfig]):
    async def run(self):
        while self._running:
            log = await fetch_from_somewhere(self.config.source)
            self.ingest(log)  # pushes into the C++ backlog, thread-safe
```

Register it in your YAML config:

```yaml
harvesters:
  - type: myapp.harvesters.MyHarvester
    name: my-source
    config:
      source: "tcp://localhost:5000"
```

## Local development

### Prerequisites

- [uv](https://docs.astral.sh/uv/)
- [Conan 2](https://docs.conan.io/2/installation.html) — `pip install conan && conan profile detect`
- CMake ≥ 3.25
- A C++23-capable compiler (GCC 14+, Clang 22+, or Homebrew LLVM on macOS)

### Build

```bash
cd py
uv sync --all-groups
```

That's it. CMake automatically invokes Conan during the configure step, so no separate Conan command is needed. The first sync compiles the C++ dependencies (SQLite3, Boost headers, yaml-cpp); subsequent syncs use Conan's binary cache and are fast 💪.

### Running tests

```bash
cd py
uv run pytest                    # all tests
uv run pytest tests/test_cli.py  # single file
```
