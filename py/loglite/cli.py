import asyncio
import threading

from typer import Option, Typer

from loglite import _core
from loglite.harvesters.manager import HarvesterManager

server_app = Typer()
migration_app = Typer()

app = Typer()
app.add_typer(server_app, name="server")
app.add_typer(migration_app, name="migrate")


async def _run_python_harvesters(config_path: str, stop_event: threading.Event) -> None:
    cfg = _core.Config.from_file(config_path)
    harvester_defs = [
        {"type": h.type, "name": h.name, "config": dict(h.config)} for h in cfg.harvesters
    ]
    mgr = HarvesterManager()
    mgr.load_harvesters(harvester_defs)

    if len(mgr) == 0:
        await asyncio.get_event_loop().run_in_executor(None, stop_event.wait)
        return

    await mgr.start_all()
    try:
        await asyncio.get_event_loop().run_in_executor(None, stop_event.wait)
    finally:
        await mgr.stop_all()


@server_app.command()
def run(config: str = Option(..., "--config", "-c")):
    stop_event = threading.Event()

    def harvester_thread():
        asyncio.run(_run_python_harvesters(config, stop_event))

    t = threading.Thread(target=harvester_thread, daemon=True)
    t.start()

    try:
        # Blocks; releases GIL inside so the harvester thread can run.
        # SIGTERM / SIGINT are handled by the C++ Asio signal_set.
        _core.run_server(config)
    finally:
        stop_event.set()
        t.join()


@migration_app.command()
def rollout(
    config: str = Option(..., "--config", "-c"),
    version_id: int = Option(-1, "--version-id", "-v"),
):
    _core.rollout(config, version_id)


@migration_app.command()
def rollback(
    config: str = Option(..., "--config", "-c"),
    version_id: int = Option(..., "--version-id", "-v"),
    force: bool = Option(False, "--force", "-f"),
):
    _core.rollback(config, version_id, force)
