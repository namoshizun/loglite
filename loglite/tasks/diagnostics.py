from loguru import logger
from loglite.config import Config
from loglite.globals import INGESTION_STATS, QUERY_STATS
from loglite.utils import repeat_every


async def register_diagnostics_task(config: Config):
    @repeat_every(seconds=(interval := config.task_diagnostics_interval))
    async def _task():
        logger.opt(colors=True).info(
            f"<dim>ingestion stats: {INGESTION_STATS.get_stats()}</dim>"
        )
        logger.opt(colors=True).info(
            f"<dim>query stats: {QUERY_STATS.get_stats()}</dim>"
        )
        INGESTION_STATS.reset()
        QUERY_STATS.reset()

    logger.opt(colors=True).info(f"<e>diagnostics task interval: {interval}s</e>")
    await _task()
