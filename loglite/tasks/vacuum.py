from loguru import logger

from datetime import datetime, timedelta
from loglite.config import Config
from loglite.database import Database
from loglite.types import QueryFilter
from loglite.utils import Timer, bytes_to_mb, repeat_every
from loglite.globals import OPERATION_LOCK


async def _remove_stale_logs(db: Database, max_age_days: int) -> int:
    now = datetime.now()
    cutoff_dtime = now - timedelta(days=max_age_days)
    min_timestamp = await db.get_min_timestamp()
    if min_timestamp.timestamp() > cutoff_dtime.timestamp():
        return 0

    filters: list[QueryFilter] = [
        {"field": "timestamp", "operator": "<=", "value": cutoff_dtime.isoformat()}
    ]
    n = await db.delete(filters)
    return n


async def _remove_excessive_logs(
    db: Database, max_size_mb: float, target_size_mb: float, batch_size: int
) -> int:
    db_size = await db.get_size_mb()
    if db_size <= max_size_mb:
        return 0

    min_id = await db.get_min_log_id()
    max_id = await db.get_max_log_id()
    rowcount = max_id - min_id + 1

    # Calculate the percentage of logs to remove
    remove_ratio = (db_size - target_size_mb) / db_size
    remove_max_id = min_id + int(rowcount * remove_ratio) - 1
    remove_count = remove_max_id - min_id + 1
    removed = 0

    # Remove the oldest logs up to the calculated threshold, in multiple chunks to avoid transaction
    # log bloat up
    logger.opt(colors=True).info(
        f"<y>[Log cleanup] db size = {db_size}MB, limit size = {max_size_mb}MB, target size = {target_size_mb}MB. "
        f"removing logs id between {min_id} and {remove_max_id} (n={remove_count}, pct={(100 * remove_ratio):.2f}%)</y>"
    )
    for start_id in range(min_id, remove_max_id, batch_size):
        end_id = min(start_id + batch_size - 1, remove_max_id)
        filters: list[QueryFilter] = [{"field": "id", "operator": "<=", "value": end_id}]
        removed += await db.delete(filters)
        logger.opt(colors=True).info(f"<y>[Log cleanup] ... already removed {removed} entries</y>")

    return removed


async def _incremental_vacuum(db: Database, max_size_mb: int) -> int:
    freelist_count = await db.get_pragma("freelist_count")
    if not freelist_count:
        return 0

    page_size = await db.get_pragma("page_size")
    max_free_count = max_size_mb * 1024 * 1024 // page_size
    free_count = min(max_free_count, freelist_count)

    async with OPERATION_LOCK, Timer("s") as timer:
        await db.incremental_vacuum(free_count)

    logger.opt(colors=True).info(
        f"<y>[Log cleanup] incremental vacuumed {free_count}/{freelist_count} pages in {timer.duration:.1f}s</y>"
    )
    remain_freelist_count = await db.get_pragma("freelist_count")
    return remain_freelist_count


async def register_database_vacuuming_task(db: Database, config: Config):
    @repeat_every(seconds=(interval := config.task_vacuum_interval))
    async def _task():
        # Do checkpoint to make sure we can then get an accurate estimate of the database size
        await db.wal_checkpoint()

        # Finish incremental vacuuming rounds first
        vacuum_mode = await db.get_pragma("auto_vacuum")
        if vacuum_mode == 2:
            remain_freelist_count = await _incremental_vacuum(db, config.task_vacuum_max_size)
            if remain_freelist_count > 0:
                return

        # Remove logs older than `vacuum_max_days`
        has_timestamp_column = any(
            column["name"] == config.log_timestamp_field for column in db.column_info
        )
        if not has_timestamp_column:
            logger.warning(
                f"log_timestamp_field: {config.log_timestamp_field} not found in columns, "
                "unable to remove stale logs based on timestamp"
            )
        else:
            n = await _remove_stale_logs(db, config.vacuum_max_days)
            if n > 0:
                logger.opt(colors=True).info(
                    f"<r>[Log cleanup] removed {n} stale logs entries (max retention days = {config.vacuum_max_days})</r>"
                )

        # Remove logs if whatever remains still exceeds `vacuum_max_size`
        n = await _remove_excessive_logs(
            db,
            bytes_to_mb(config.vacuum_max_size_bytes),
            bytes_to_mb(config.vacuum_target_size_bytes),
            config.vacuum_delete_batch_size,
        )

        if n > 0:
            db_size = await db.get_size_mb()
            logger.opt(colors=True).info(
                f"<r>[Log cleanup] removed {n} logs entries, database size is now {db_size}MB</r>"
            )

        if vacuum_mode == 1:
            # Do full vacuuming
            async with OPERATION_LOCK, Timer("s") as timer:
                await db.vacuum()
                await db.wal_checkpoint("FULL")

            logger.opt(colors=True).info(
                f"<y>[Log cleanup] full vacuumed the database in {timer.duration:.1f}s</y>"
            )

    logger.opt(colors=True).info(f"<e>database vacuuming task interval: {interval}s</e>")
    await _task()
