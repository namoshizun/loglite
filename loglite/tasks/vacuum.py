from loguru import logger

from datetime import datetime, timedelta
from loglite.config import Config
from loglite.database import Database
from loglite.types import QueryFilter
from loglite.utils import repeat_every


async def __remove_stale_logs(db: Database, max_age_days: int) -> int:
    now = datetime.now()
    cutoff = now - timedelta(days=max_age_days)
    filters: list[QueryFilter] = [
        {"field": "timestamp", "operator": "<=", "value": cutoff.isoformat()}
    ]
    return await db.delete(filters)


async def __remove_excessive_logs(
    db: Database, max_size_mb: int, target_size_mb: int
) -> int:
    db_size = await db.get_size_mb()
    if db_size <= max_size_mb:
        return 0

    min_id = await db.get_min_log_id()
    max_id = await db.get_max_log_id()
    count = max_id - min_id + 1

    # Calculate the percentage of logs to remove
    remove_pct = (db_size - target_size_mb) / db_size
    remove_count = int(count * remove_pct)
    remove_before_id = min_id + remove_count - 1

    # Remove the oldest logs up to the calculated threshold
    filters: list[QueryFilter] = [
        {"field": "id", "operator": "<=", "value": remove_before_id}
    ]
    return await db.delete(filters)


async def register_database_vacuuming_task(db: Database, config: Config):
    @repeat_every(seconds=(interval := config.task_vacuum_interval))
    async def _task():
        # Do checkpoint to make sure we can then get an accurate estimate of the database size
        await db.wal_checkpoint()

        # Remove logs older than `vacuum_max_days`
        columns = await db.get_log_columns()
        has_timestamp_column = any(
            column["name"] == config.log_timestamp_field for column in columns
        )
        if not has_timestamp_column:
            logger.warning(
                f"log_timestamp_field: {config.log_timestamp_field} not found in columns, "
                "unable to remove stale logs based on timestamp"
            )
        else:
            n = await __remove_stale_logs(db, config.vacuum_max_days)
            if n > 0:
                logger.opt(colors=True).info(
                    f"<r>[Log cleanup] removed {n} stale logs entries (max retention days = {config.vacuum_max_days})</r>"
                )

        # Remove logs if whatever remains still exceeds `vacuum_max_size`
        n = await __remove_excessive_logs(
            db, config.vacuum_max_size_bytes, config.vacuum_target_size_bytes
        )

        if n > 0:
            db_size = await db.get_size_mb()
            logger.opt(colors=True).info(
                f"<r>[Log cleanup] removed {n} logs entries, database size is now {db_size}MB</r>"
            )

    logger.opt(colors=True).info(
        f"<e>database vacuuming task interval: {interval}s</e>"
    )
    await _task()
