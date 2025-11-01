import asyncio
from contextlib import suppress
from loguru import logger

from loglite.config import Config
from loglite.database import Database
from loglite.globals import INGESTION_STATS, BACKLOG, LAST_INSERT_LOG_ID, OPERATION_LOCK
from loglite.utils import Timer


async def register_flushing_backlog_task(db: Database, config: Config):
    logger.opt(colors=True).info("<e>backlog flushing task started</e>")

    with suppress(asyncio.CancelledError):
        while True:
            backlog = BACKLOG.instance()
            with suppress(asyncio.TimeoutError, TimeoutError):
                await asyncio.wait_for(
                    backlog.full_signal.wait(), timeout=config.task_backlog_flush_interval
                )

            logs = await backlog.flush()
            if not logs:
                continue

            if config.debug:
                logger.info(f"🧹 flushing {len(logs)} logs from backlog")

            async with OPERATION_LOCK:
                with Timer("ms") as timer:
                    count = await db.insert(logs)
                    max_log_id = await db.get_max_log_id()

            INGESTION_STATS.collect(count, timer.duration)
            await LAST_INSERT_LOG_ID.set(max_log_id - count + 1)
