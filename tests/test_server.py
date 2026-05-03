from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
from pathlib import Path
from typing import Any

import orjson
import pytest
import pytest_asyncio
from aiohttp.test_utils import TestClient, TestServer

from loglite.config import Config
from loglite.globals import BACKLOG, LAST_INSERT_LOG_ID
from loglite.handlers.query import SubscribeLogsSSEHandler
from loglite.server import LogLiteServer
from loglite.types import QueryFilter


class FakeDatabase:
    def __init__(self):
        self.ping_error: Exception | None = None
        self.query_error: Exception | None = None
        self.query_result = {
            "total": 1,
            "offset": 3,
            "limit": 5,
            "results": [{"id": 7, "message": "hello"}],
        }
        self.queries: list[dict[str, Any]] = []

    async def ping(self) -> bool:
        if self.ping_error:
            raise self.ping_error
        return True

    async def query(
        self,
        fields: list[str],
        filters: list[QueryFilter],
        offset: int = 0,
        limit: int = 100,
    ) -> dict[str, Any]:
        if self.query_error:
            raise self.query_error

        self.queries.append(
            {"fields": fields, "filters": filters, "offset": offset, "limit": limit}
        )
        return self.query_result

    async def get_max_log_id(self) -> int:
        return 7


@pytest_asyncio.fixture
async def server_client(tmp_path: Path) -> AsyncIterator[tuple[TestClient, FakeDatabase]]:
    db = FakeDatabase()
    server = LogLiteServer(
        db=db,  # pyright: ignore[reportArgumentType]
        config=Config(migrations=[], sqlite_dir=tmp_path),
    )
    server._setup_logging()
    await server._setup_globals()
    await server._setup_routes()

    client = TestClient(TestServer(server.app))
    await client.start_server()
    yield client, db
    await client.close()
    BACKLOG.reset()


async def read_json(response) -> dict[str, Any]:
    return orjson.loads(await response.read())


@pytest.mark.asyncio
async def test_health_check(server_client: tuple[TestClient, FakeDatabase]):
    client, db = server_client

    response = await client.get("/health")
    assert response.status == 200
    assert await read_json(response) == {"status": "success", "result": "ok"}

    db.ping_error = RuntimeError("database unavailable")
    response = await client.get("/health")
    assert response.status == 500
    assert await read_json(response) == {
        "status": "error",
        "error": "database unavailable",
    }


@pytest.mark.asyncio
async def test_insert_logs_pushes_payload_to_backlog(
    server_client: tuple[TestClient, FakeDatabase],
):
    client, _ = server_client
    log = {"timestamp": "2026-05-03T09:35:00+08:00", "level": "INFO", "message": "hello"}

    response = await client.post(
        "/logs",
        data=orjson.dumps(log),
        headers={"Content-Type": "application/json"},
    )

    assert response.status == 200
    assert await read_json(response) == {"status": "success", "result": "ok"}
    assert list(BACKLOG.instance().value) == [log]


@pytest.mark.asyncio
async def test_insert_logs_rejects_invalid_json(server_client: tuple[TestClient, FakeDatabase]):
    client, _ = server_client

    response = await client.post(
        "/logs",
        data=b"{",
        headers={"Content-Type": "application/json"},
    )

    assert response.status == 500
    payload = await read_json(response)
    assert payload["status"] == "error"
    assert "unexpected end of data" in payload["error"]
    assert list(BACKLOG.instance().value) == []


@pytest.mark.asyncio
async def test_query_logs_passes_filters_and_pagination_to_database(
    server_client: tuple[TestClient, FakeDatabase],
):
    client, db = server_client

    response = await client.get(
        "/logs",
        params=[
            ("fields", "id,message"),
            ("offset", "3"),
            ("limit", "5"),
            ("level", "=INFO,!=DEBUG"),
            ("message", "~=hello"),
        ],
    )

    assert response.status == 200
    assert await read_json(response) == {"status": "success", "result": db.query_result}
    assert db.queries == [
        {
            "fields": ["id", "message"],
            "offset": 3,
            "limit": 5,
            "filters": [
                {"field": "level", "operator": "=", "value": "INFO"},
                {"field": "level", "operator": "!=", "value": "DEBUG"},
                {"field": "message", "operator": "~=", "value": "hello"},
            ],
        }
    ]


@pytest.mark.asyncio
async def test_query_logs_defaults_to_wildcard_fields(
    server_client: tuple[TestClient, FakeDatabase],
):
    client, db = server_client

    response = await client.get("/logs", params={"fields": "*", "offset": "0", "limit": "100"})

    assert response.status == 200
    assert db.queries[-1] == {"fields": ["*"], "filters": [], "offset": 0, "limit": 100}


@pytest.mark.asyncio
async def test_query_logs_validates_required_parameters(
    server_client: tuple[TestClient, FakeDatabase],
):
    client, db = server_client

    response = await client.get("/logs", params={"fields": "*", "offset": "0"})

    assert response.status == 400
    assert await read_json(response) == {
        "status": "error",
        "error": "Invalid request. Details: Required parameter 'limit' is missing",
    }
    assert db.queries == []


@pytest.mark.asyncio
async def test_query_logs_validates_filter_expressions(
    server_client: tuple[TestClient, FakeDatabase],
):
    client, db = server_client

    response = await client.get(
        "/logs",
        params={"fields": "*", "offset": "0", "limit": "10", "level": "INFO"},
    )

    assert response.status == 400
    assert await read_json(response) == {
        "status": "error",
        "error": "Invalid request. Details: Field 'level' has invalid filter expression: INFO",
    }
    assert db.queries == []


@pytest.mark.asyncio
async def test_query_logs_rejects_non_integer_pagination(
    server_client: tuple[TestClient, FakeDatabase],
):
    client, db = server_client

    response = await client.get(
        "/logs",
        params={"fields": "*", "offset": "abc", "limit": "10"},
    )

    assert response.status == 500
    payload = await read_json(response)
    assert payload["status"] == "error"
    assert "invalid literal for int()" in payload["error"]
    assert db.queries == []


@pytest.mark.asyncio
async def test_query_logs_surfaces_database_errors(
    server_client: tuple[TestClient, FakeDatabase],
):
    client, db = server_client
    db.query_error = RuntimeError("query exploded")

    response = await client.get("/logs", params={"fields": "*", "offset": "0", "limit": "1"})

    assert response.status == 500
    assert await read_json(response) == {"status": "error", "error": "query exploded"}


class SSEConfig:
    debug = True
    sse_limit = 2
    sse_debounce_ms = 1


class SSERequest:
    def __init__(self, fields: list[str]):
        self.validated_data = fields


class SSETransport:
    def __init__(self):
        self.sent: list[str] = []
        self.connected = True

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        return False

    def is_connected(self) -> bool:
        return self.connected

    async def send(self, data: str):
        self.sent.append(data)
        self.connected = False


@pytest.mark.asyncio
async def test_subscribe_logs_sse_pushes_new_logs(monkeypatch: pytest.MonkeyPatch):
    db = FakeDatabase()
    db.query_result = {"total": 1, "offset": 0, "limit": 2, "results": [{"id": 8}]}
    transport = SSETransport()
    monkeypatch.setattr("loglite.handlers.query.sse_response", lambda request: transport)
    await LAST_INSERT_LOG_ID.set(7)
    handler = SubscribeLogsSSEHandler(db, SSEConfig())  # pyright: ignore[reportArgumentType]

    async def publish_update():
        await asyncio.sleep(0)
        await LAST_INSERT_LOG_ID.set(8)

    publisher = asyncio.create_task(publish_update())
    response = await handler.handle(SSERequest(["id"]))  # pyright: ignore[reportArgumentType]
    await publisher

    assert response is transport
    assert transport.sent == ['[{"id":8}]']
    assert db.queries == [
        {
            "fields": ["id"],
            "filters": [
                {"field": "id", "operator": ">", "value": 7},
                {"field": "id", "operator": "<=", "value": 8},
            ],
            "offset": 0,
            "limit": 2,
        }
    ]
    assert LAST_INSERT_LOG_ID.get_subscribers_count() == 0


@pytest.mark.asyncio
async def test_subscribe_logs_sse_returns_400_on_client_error(
    monkeypatch: pytest.MonkeyPatch,
):
    from aiohttp.client import ClientError

    class BrokenSSETransport:
        async def __aenter__(self):
            raise ClientError

        async def __aexit__(self, exc_type, exc, tb):
            return False

    monkeypatch.setattr("loglite.handlers.query.sse_response", lambda request: BrokenSSETransport())
    await LAST_INSERT_LOG_ID.set(1)
    handler = SubscribeLogsSSEHandler(FakeDatabase(), SSEConfig())  # pyright: ignore[reportArgumentType]

    response = await handler.handle(SSERequest(["*"]))  # pyright: ignore[reportArgumentType]

    assert response.status == 400
    assert LAST_INSERT_LOG_ID.get_subscribers_count() == 0


@pytest.mark.asyncio
async def test_start_creates_runner_and_site(tmp_path: Path):
    server = LogLiteServer(
        db=FakeDatabase(),  # pyright: ignore[reportArgumentType]
        config=Config(migrations=[], sqlite_dir=tmp_path, port=0),
    )

    runner, site = await server.start()

    assert runner.addresses
    assert site.name.startswith("http://127.0.0.1:")
    await runner.cleanup()
