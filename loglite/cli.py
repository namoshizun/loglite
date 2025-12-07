import asyncio
import signal
from loguru import logger
from typer import Typer, Option

from loglite.database import Database
from loglite.migrations import MigrationManager
from loglite.config import Config
from loglite.server import LogLiteServer


server_app = Typer()
migration_app = Typer()

app = Typer()
app.add_typer(server_app, name="server")
app.add_typer(migration_app, name="migrate")


async def _shutdown(signal, loop: asyncio.AbstractEventLoop):
    """Shutdown the server gracefully"""
    logger.info(f"Received exit signal {signal.name}...")
    logger.info("Gracefully shutting down...")

    tasks = [t for t in asyncio.all_tasks() if t is not asyncio.current_task()]

    for task in tasks:
        task.cancel()

    await asyncio.gather(*tasks, return_exceptions=True)
    loop.stop()


async def _run_server(config_path: str):
    config = Config.from_file(config_path)
    async with Database(config) as db:
        await db.initialize()
        server = LogLiteServer(db, config)
        await server.setup()

        # Handle shutdown signals
        loop = asyncio.get_event_loop()
        exit_signals = (signal.SIGHUP, signal.SIGTERM, signal.SIGINT)
        for s in exit_signals:
            loop.add_signal_handler(s, lambda s=s: asyncio.create_task(_shutdown(s, loop)))

        runner, _ = await server.start()

        try:
            while True:
                await asyncio.sleep(3600)
        except asyncio.CancelledError:
            pass
        finally:
            await runner.cleanup()


async def _rollout(config_path: str, start_version: int = -1):
    config = Config.from_file(config_path)
    async with Database(config) as db:
        await db.initialize()
        migration_manager = MigrationManager(db, config.migrations)
        await migration_manager.apply_pending_migrations(start_version)


async def _rollback(config_path: str, version_id: int, force: bool = False):
    config = Config.from_file(config_path)
    async with Database(config) as db:
        await db.initialize()
        migration_manager = MigrationManager(db, config.migrations)
        await migration_manager.rollback_migration(version_id, force)


@server_app.command()
def run(config: str = Option(..., "--config", "-c")):
    asyncio.run(_run_server(config))


@migration_app.command()
def rollout(
    config: str = Option(..., "--config", "-c"),
    version_id: int = Option(-1, "--version-id", "-v"),
):
    asyncio.run(_rollout(config, version_id))


@migration_app.command()
def rollback(
    config: str = Option(..., "--config", "-c"),
    version_id: int = Option(None, "--version-id", "-v"),
    force: bool = Option(False, "--force", "-f"),
):
    asyncio.run(_rollback(config, version_id, force))
