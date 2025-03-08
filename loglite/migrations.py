from typing import List, Dict, Any, TypedDict
from loguru import logger

from .database import Database
from .types import Migration


class MigrationManager:
    def __init__(self, db: Database, migrations_config: List[Dict[str, Any]]):
        self.db = db
        self.migrations_config = migrations_config

    async def apply_pending_migrations(self) -> bool:
        """Apply all pending migrations"""
        applied_versions = await self.db.get_applied_versions()

        # Sort migrations by version
        sorted_migrations = sorted(self.migrations_config, key=lambda m: m["version"])

        success = True
        for migration in sorted_migrations:
            version = migration["version"]
            if version not in applied_versions:
                logger.info(f"Applying migration version {version}")
                statements = migration.get("rollout", [])
                if statements:
                    if not await self.db.apply_migration(version, statements):
                        success = False
                        break

        return success

    async def rollback_migration(self, version: int) -> bool:
        """Rollback a specific migration version"""
        applied_versions = await self.db.get_applied_versions()

        if version not in applied_versions:
            logger.warning(
                f"Migration version {version} not applied, nothing to rollback"
            )
            return False

        for migration in self.migrations_config:
            if migration["version"] == version:
                statements = migration.get("rollback", [])
                if statements:
                    return await self.db.rollback_migration(version, statements)
                else:
                    logger.warning(f"No rollback statements for version {version}")
                    return False

        logger.warning(f"Migration version {version} not found in configuration")
        return False
