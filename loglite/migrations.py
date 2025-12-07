from loguru import logger
from typing import TYPE_CHECKING

from loglite.types import Migration

if TYPE_CHECKING:
    from loglite.database import Database


class MigrationManager:
    def __init__(self, db: "Database", migrations_config: list[Migration]):
        self.db = db
        self.migrations_config = migrations_config

    async def apply_pending_migrations(self, start_version: int = -1) -> bool:
        """Apply all pending migrations"""
        applied_versions = await self.db.get_applied_versions()

        # Sort migrations by version
        pending_migrations = [m for m in self.migrations_config if m["version"] > start_version]
        pending_migrations = sorted(pending_migrations, key=lambda m: m["version"])

        for migration in pending_migrations:
            version = migration["version"]
            if version not in applied_versions:
                logger.info(f" Applying migration version {version}...")
                if statements := migration.get("rollout", []):
                    return await self.db.apply_migration(version, statements)
                else:
                    logger.warning(f"🤔 No rollout statements for version {version}")

        return True

    async def rollback_migration(self, version: int, force: bool = False) -> bool:
        """Rollback a specific migration version"""
        applied_versions = await self.db.get_applied_versions()

        if version not in applied_versions:
            logger.warning(f" 🤷‍♂️ Migration version {version} not applied, nothing to rollback")
            return False

        for migration in self.migrations_config:
            if migration["version"] == version:
                statements = migration.get("rollback", [])
                if statements:
                    if not force:
                        ans = input(
                            f"Statements: {statements}\n"
                            f"🤔 Are you sure you want to rollback migration version {version}? (y/n)"
                        )
                        if ans != "y":
                            logger.warning(f"Migration version {version} not rolled back")
                            return False

                    return await self.db.rollback_migration(version, statements)
                else:
                    logger.warning(f"🤔 No rollback statements for version {version}")

        logger.warning(f"🤔 Migration version {version} not found in configuration")
        return False
