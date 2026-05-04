import asyncio
import os
from pathlib import Path

import orjson
import pytest

from loglite.harvesters.file import FileHarvester, FileHarvesterConfig


@pytest.mark.asyncio
async def test_file_harvester_rotation(tmp_path: Path, captured_logs: list[dict]):
    log_file: Path = tmp_path / "test.log"
    log_file.touch()

    harvester = FileHarvester("rotation_test", FileHarvesterConfig(path=log_file))
    await harvester.start()
    await asyncio.sleep(0.1)

    # 1. Write initial logs
    log1 = {"message": "log1", "timestamp": "2023-01-01T00:00:01"}
    with log_file.open("ab") as f:
        f.write(orjson.dumps(log1) + b"\n")
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(0.5)
    assert len(captured_logs) == 1
    assert captured_logs[0]["message"] == "log1"

    # 2. Rotate log file (rename → new file)
    rotated_file = tmp_path / "test.log.1"
    log_file.rename(rotated_file)
    log_file.touch()

    log2 = {"message": "log2", "timestamp": "2023-01-01T00:00:02"}
    with log_file.open("ab") as f:
        f.write(orjson.dumps(log2) + b"\n")
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(1.0)

    assert len(captured_logs) == 2
    assert captured_logs[1]["message"] == "log2"

    await harvester.stop()


@pytest.mark.asyncio
async def test_file_harvester_truncation(tmp_path: Path, captured_logs: list[dict]):
    log_file: Path = tmp_path / "test_trunc.log"
    log_file.touch()

    harvester = FileHarvester("truncation_test", FileHarvesterConfig(path=log_file))
    await harvester.start()
    await asyncio.sleep(0.1)

    log1 = {"message": "log1", "timestamp": "2023-01-01T00:00:01"}
    with log_file.open("ab") as f:
        f.write(orjson.dumps(log1) + b"\n")
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(0.5)
    assert len(captured_logs) == 1

    # Truncate
    with log_file.open("wb") as f:
        f.truncate(0)
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(0.5)

    log2 = {"message": "log2", "timestamp": "2023-01-01T00:00:02"}
    with log_file.open("ab") as f:
        f.write(orjson.dumps(log2) + b"\n")
        f.flush()
        os.fsync(f.fileno())

    await asyncio.sleep(1.0)

    assert len(captured_logs) == 2
    assert captured_logs[1]["message"] == "log2"

    await harvester.stop()
