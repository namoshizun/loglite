import asyncio
from contextlib import suppress
from loguru import logger
from loglite.config import Config
from loglite.database import Database
from loglite.globals import INGESTION_STATS, BACKLOG, LAST_INSERT_LOG_ID
from loglite.utils import Timer


async def register_flushing_backlog_task(db: Database, config: Config):
    interval = config.task_backlog_flush_interval

    async def _task():
        evt = BACKLOG.full_signal
        while True:
            with suppress(asyncio.TimeoutError, TimeoutError):
                await asyncio.wait_for(evt.wait(), timeout=interval)

            logs = await BACKLOG.flush()
            if not logs:
                continue

            if config.debug:
                logger.info(f"🧹 flushing {len(logs)} logs from backlog")

            with Timer("ms") as timer:
                count = await db.insert(logs)
                max_log_id = await db.get_max_log_id()

            INGESTION_STATS.collect(count, timer.duration)
            await LAST_INSERT_LOG_ID.set(max_log_id - count + 1)

    logger.opt(colors=True).info(f"<e>flushing backlog task interval: {interval}s</e>")
    await _task()
