from loglite.utils import AtomicMutableValue, StatsTracker


LAST_INSERT_LOG_ID = AtomicMutableValue[int](0)

INGESTION_STATS = StatsTracker(period_seconds=10)

QUERY_STATS = StatsTracker(period_seconds=10)
