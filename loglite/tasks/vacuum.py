from loguru import logger

from loglite.config import Config
from loglite.database import Database


async def __remove_stale_logs(db: Database, max_age_days: int) -> int:
    return 0


async def __remove_excessive_logs(
    db: Database, max_size_mb: int, target_size_mb: int
) -> int:
    db_size = await db.get_size_mb()
    return 0


async def database_vacuum(db: Database, config: Config):
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
                f"<r>[Log cleanup] removed {n} stale logs entries</r>"
            )

    # Remove logs if whatever remains still exceeds `vacuum_max_size`
    n = await __remove_excessive_logs(
        db, config.vacuum_max_size_bytes, config.vacuum_target_size_bytes
    )
    db_size = await db.get_size_mb()

    if n > 0:
        logger.opt(colors=True).info(
            f"<r>[Log cleanup] removed {n} logs entries, database size is now {db_size}MB</r>"
        )
