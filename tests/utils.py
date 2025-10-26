import string
import random
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
