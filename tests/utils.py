import time
import string
import random
import json
import loguru
from loguru import logger
from pathlib import Path
from loglite.database import Database
from datetime import datetime


async def bloat_db(count: int, db: Database):
    batch_size = 500
    for _ in range(0, count, batch_size):
        random_msg = "".join(random.choices(string.ascii_letters + string.digits, k=100))
        await db.insert(
            [
                {
                    "timestamp": datetime.now(),
                    "message": random_msg,
                    "level": "INFO",
                    "service": "test",
                    "filename": "test.py",
                    "path": "test.py",
                    "line": 1,
                    "function": "test_function",
                }
                for _ in range(batch_size)
            ]
        )


def mock_log_file(path: Path, count: int = 1000, duration: int = 5):
    def loglite_serializer(record):
        return (
            json.dumps(
                {
                    "timestamp": record["time"].isoformat(),
                    "message": record["message"],
                    "level": record["level"].name,
                    "service": "test",
                    "filename": "test.py",
                    "path": "test.py",
                }
            )
            + "\n"
        )

    path.parent.mkdir(exist_ok=True, parents=True)
    logger.remove()
    logger.add(path, serialize=True)
    loguru._handler.Handler._serialize_record = staticmethod(
        lambda _, record: loglite_serializer(record)
    )
    entry_interval = duration / count
    for idx in range(count):
        logger.info(f"test message {idx}")
        time.sleep(entry_interval)
