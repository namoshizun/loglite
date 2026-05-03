from __future__ import annotations

import asyncio
from typing import Any

import pytest

from loglite.column_dict import ColumnDictionary
from loglite.types import QueryFilter


class DictRow(dict):
    def __getitem__(self, key: str) -> Any:
        return super().__getitem__(key)


class DictDatabase:
    def __init__(self):
        self.rows = [
            DictRow(column="level", value="INFO", value_id=1),
            DictRow(column="level", value="ERROR", value_id=2),
            DictRow(column="service", value="api", value_id=1),
        ]
        self.inserted: list[tuple[str, Any, int]] = []

    async def get_column_dict_table(self) -> list[DictRow]:
        return self.rows

    async def insert_column_dict_value(self, col: str, value: Any, id: int):
        self.inserted.append((col, value, id))


async def drain_column_dict_writes():
    await asyncio.sleep(0)


@pytest.mark.asyncio
async def test_column_dictionary_loads_existing_entries():
    db = DictDatabase()

    column_dict = await ColumnDictionary.load(db)  # pyright: ignore[reportArgumentType]

    assert column_dict.get_lookup() == {
        "level": {"INFO": 1, "ERROR": 2},
        "service": {"api": 1},
    }
    assert column_dict.get_value("level", 2) == "ERROR"


@pytest.mark.asyncio
async def test_column_dictionary_get_or_create_reuses_and_persists_values():
    db = DictDatabase()
    column_dict = await ColumnDictionary.load(db)  # pyright: ignore[reportArgumentType]

    assert column_dict.get_or_create("level", "INFO") == 1
    assert column_dict.get_or_create("level", "WARN") == 3
    assert column_dict.get_or_create("host", "node-1") == 1
    await drain_column_dict_writes()

    assert column_dict.get_lookup()["level"]["WARN"] == 3
    assert column_dict.get_lookup()["host"]["node-1"] == 1
    assert db.inserted == [("level", "WARN", 3), ("host", "node-1", 1)]


@pytest.mark.parametrize(
    ("filter_", "expected_ids"),
    [
        ({"field": "level", "operator": "=", "value": "INFO"}, [1]),
        ({"field": "level", "operator": "!=", "value": "INFO"}, [2]),
        ({"field": "latency", "operator": ">", "value": 100}, [2, 3]),
        ({"field": "latency", "operator": ">=", "value": 100}, [1, 2, 3]),
        ({"field": "latency", "operator": "<", "value": 100}, []),
        ({"field": "latency", "operator": "<=", "value": 100}, [1]),
        ({"field": "message", "operator": "~=", "value": "failed"}, [2]),
    ],
)
def test_column_dictionary_query_candidates(filter_: QueryFilter, expected_ids: list[int]):
    column_dict = ColumnDictionary(
        db=None,  # pyright: ignore[reportArgumentType]
        lookup={
            "level": {"INFO": 1, "ERROR": 2},
            "latency": {100: 1, 250: 2, 500: 3},
            "message": {"user login": 1, "request failed": 2},
        },
    )

    assert column_dict.query_candidates(filter_) == expected_ids
