import pytest
import asyncio
import os
import orjson
from pathlib import Path
from loglite.harvesters.file import FileHarvester, FileHarvesterConfig
from loglite.globals import BACKLOG
from loglite.backlog import Backlog


@pytest.fixture
def backlog():
    b = Backlog(100)
    BACKLOG.set(b)
    return b


@pytest.mark.asyncio
async def test_file_harvester_rotation(tmp_path, backlog):
    log_file: Path = tmp_path / "test.log"
    log_file.touch()

    harvester = FileHarvester("rotation_test", FileHarvesterConfig(path=log_file))
    await harvester.start()

    # Wait for harvester to be ready
    await asyncio.sleep(0.1)

    # 1. Write initial logs
    log1 = {"message": "log1", "timestamp": "2023-01-01T00:00:01"}
    with log_file.open("ab") as f:
        f.write(orjson.dumps(log1) + b"\n")
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(0.5)
    assert len(backlog.value) == 1
    assert backlog.value[0]["message"] == "log1"

    # 2. Rotate log file
    rotated_file = tmp_path / "test.log.1"
    log_file.rename(rotated_file)

    # Create new file
    log_file.touch()

    # Write to new file
    log2 = {"message": "log2", "timestamp": "2023-01-01T00:00:02"}
    with log_file.open("ab") as f:
        f.write(orjson.dumps(log2) + b"\n")
        f.flush()
        os.fsync(f.fileno())

    # Wait for harvester to detect rotation
    await asyncio.sleep(1.0)

    assert len(backlog.value) == 2
    assert backlog.value[1]["message"] == "log2"

    await harvester.stop()


@pytest.mark.asyncio
async def test_file_harvester_truncation(tmp_path, backlog):
    log_file: Path = tmp_path / "test_trunc.log"
    log_file.touch()

    harvester = FileHarvester("truncation_test", FileHarvesterConfig(path=log_file))
    await harvester.start()
    await asyncio.sleep(0.1)

    # 1. Write logs
    log1 = {"message": "log1", "timestamp": "2023-01-01T00:00:01"}
    with log_file.open("ab") as f:
        f.write(orjson.dumps(log1) + b"\n")
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(0.5)
    assert len(backlog.value) == 1

    # 2. Truncate file
    with log_file.open("wb") as f:
        f.truncate(0)
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(0.5)

    # 3. Write new logs
    log2 = {"message": "log2", "timestamp": "2023-01-01T00:00:02"}
    with log_file.open("ab") as f:
        f.write(orjson.dumps(log2) + b"\n")
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(1.0)

    # Should have picked up log2
    assert len(backlog.value) == 2
    assert backlog.value[1]["message"] == "log2"

    await harvester.stop()
