import asyncio
import signal
from loguru import logger
from typer import Typer, Option

from .config import Config
from .server import LogLiteServer


server_app = Typer()
migration_app = Typer()

app = Typer()
app.add_typer(server_app, name="server")
app.add_typer(migration_app, name="migrate")


async def _shutdown(signal, loop):
    """Shutdown the server gracefully"""
    logger.info(f"Received exit signal {signal.name}...")
    logger.info("Shutting down...")

    tasks = [t for t in asyncio.all_tasks() if t is not asyncio.current_task()]

    for task in tasks:
        task.cancel()

    await asyncio.gather(*tasks, return_exceptions=True)
    loop.stop()


async def _run_server(config_path: str):
    config = Config.from_file(config_path)
    server = LogLiteServer(config)
    await server.setup()

    # Handle shutdown signals
    loop = asyncio.get_event_loop()
    signals = (signal.SIGHUP, signal.SIGTERM, signal.SIGINT)
    for s in signals:
        loop.add_signal_handler(s, lambda s=s: asyncio.create_task(_shutdown(s, loop)))

    runner, _ = await server.start()

    try:
        while True:
            await asyncio.sleep(3600)
    finally:
        await runner.cleanup()


async def _migrate(config_path: str):
    config = Config.from_file(config_path)


async def _rollback(config_path: str):
    config = Config.from_file(config_path)


@server_app.command()
def run(config: str = Option(..., "--config", "-c")):
    asyncio.run(_run_server(config))


@migration_app.command()
def rollout(config: str = Option(..., "--config", "-c")):
    asyncio.run(_migrate(config))


@migration_app.command()
def rollback(config: str = Option(..., "--config", "-c")):
    asyncio.run(_rollback(config))
