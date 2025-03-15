from loguru import logger
from loglite.globals import INGESTION_STATS, QUERY_STATS


async def run_diagnostics():
    logger.opt(colors=True).info(
        f"<dim>ingestion stats: {INGESTION_STATS.get_stats()}</dim>"
    )
    logger.opt(colors=True).info(f"<dim>query stats: {QUERY_STATS.get_stats()}</dim>")
    INGESTION_STATS.reset()
    QUERY_STATS.reset()
