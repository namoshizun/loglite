from loguru import logger

from datetime import datetime, timedelta
from loglite.config import Config
from loglite.database import Database
from loglite.types import QueryFilter
from loglite.utils import bytes_to_mb, repeat_every


async def __remove_stale_logs(db: Database, max_age_days: int) -> int:
    now = datetime.now()
    cutoff = now - timedelta(days=max_age_days)
    filters: list[QueryFilter] = [
        {"field": "timestamp", "operator": "<=", "value": cutoff.isoformat()}
    ]
    n = await db.delete(filters)
    await db.vacuum()
    return n


async def __remove_excessive_logs(
    db: Database, max_size_mb: float, target_size_mb: float
) -> int:
    db_size = await db.get_size_mb()
    if db_size <= max_size_mb:
        return 0

    min_id = await db.get_min_log_id()
    max_id = await db.get_max_log_id()
    count = max_id - min_id + 1

    # Calculate the percentage of logs to remove
    remove_ratio = (db_size - target_size_mb) / db_size
    remove_count = int(count * remove_ratio)
    remove_before_id = min_id + remove_count - 1

    # Remove the oldest logs up to the calculated threshold
    filters: list[QueryFilter] = [
        {"field": "id", "operator": "<=", "value": remove_before_id}
    ]

    logger.opt(colors=True).info(
        f"<y>[Log cleanup] db size = {db_size}MB, limit size = {max_size_mb}MB, target size = {target_size_mb}MB. "
        f"removing logs id between {min_id} and {remove_before_id} (n={remove_count}, pct={(100 * remove_ratio):.2f}%)</y>"
    )
    n = await db.delete(filters)
    await db.vacuum()
    return n


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
            db,
            bytes_to_mb(config.vacuum_max_size_bytes),
            bytes_to_mb(config.vacuum_target_size_bytes),
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
