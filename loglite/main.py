import asyncio
import logging
import argparse
import os
import sys
import signal

from .config import Config
from .server import LoggingServer

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)

logger = logging.getLogger(__name__)


async def main():
    parser = argparse.ArgumentParser(description="Logging Server")
    parser.add_argument(
        "--config", type=str, default="config.yaml", help="Path to configuration file"
    )
    args = parser.parse_args()

    # Load configuration
    config = Config.from_file(args.config)

    # Create and start server
    server = LoggingServer(config)
    await server.setup()

    # Handle shutdown signals
    loop = asyncio.get_event_loop()
    signals = (signal.SIGHUP, signal.SIGTERM, signal.SIGINT)
    for s in signals:
        loop.add_signal_handler(s, lambda s=s: asyncio.create_task(shutdown(s, loop)))

    runner, site = await server.start()

    try:
        # Keep running until interrupted
        while True:
            await asyncio.sleep(3600)  # Sleep for an hour
    finally:
        await runner.cleanup()


async def shutdown(signal, loop):
    """Shutdown the server gracefully"""
    logger.info(f"Received exit signal {signal.name}...")
    logger.info("Shutting down...")

    tasks = [t for t in asyncio.all_tasks() if t is not asyncio.current_task()]

    for task in tasks:
        task.cancel()

    await asyncio.gather(*tasks, return_exceptions=True)
    loop.stop()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Server stopped by keyboard interrupt")
    except Exception as e:
        logger.exception(f"Unexpected error: {e}")
