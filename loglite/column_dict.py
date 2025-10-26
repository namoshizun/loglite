from __future__ import annotations

import asyncio
import sys
import operator
from typing import Any, TYPE_CHECKING
from loguru import logger
from loglite.types import QueryFilter
from loglite.utils import bytes_to_mb

if TYPE_CHECKING:
    from loglite.database import Database


ColumnName = str
ColumnValue = Any
ColumnValueId = int

LookupTable = dict[ColumnName, dict[ColumnValue, ColumnValueId]]


class ColumnDictionary:
    def __init__(self, db: "Database", lookup: LookupTable):
        self.db = db
        self.__lookup: LookupTable = lookup

    @classmethod
    async def load(cls, db: "Database") -> "ColumnDictionary":
        lookup: LookupTable = {}
        count = 0
        for row in await db.get_column_dict_table():
            col = row["column"]
            if col not in lookup:
                lookup[col] = {}

            lookup[col][row["value"]] = row["value_id"]
            count += 1

        mem_size = sys.getsizeof(lookup)
        logger.info(
            f"🔍 Loaded column dictionary. Total entry count: {count}, memory size: {bytes_to_mb(mem_size):.2f} MB"
        )
        return cls(db, lookup)

    def get_or_create(self, col: ColumnName, value: ColumnValue) -> ColumnValueId:
        if col not in self.__lookup:
            # Create a new column value entry
            value_id = 1
            asyncio.create_task(self.db.insert_column_dict_value(col, value, value_id))
            self.__lookup[col] = {value: value_id}

        if value_id := self.__lookup[col].get(value):
            return value_id

        # This is a new value for an existing column, so we need to create a new entry
        # and update the lookup table
        max_id = max(self.__lookup[col].values())
        value_id = max_id + 1
        self.__lookup[col][value] = value_id
        asyncio.create_task(self.db.insert_column_dict_value(col, value, value_id))

        return value_id

    def get_value(self, col: str, id: int) -> ColumnValue:
        for _v, _id in self.__lookup[col].items():
            if _id == id:
                return _v

    def get_lookup(self) -> LookupTable:
        return self.__lookup

    def query_candidates(self, filter: QueryFilter) -> list[int]:
        col = filter["field"]
        op = filter["operator"]
        value = filter["value"]

        candidates_tbl = self.__lookup[col]
        ids = []
        for v, id in candidates_tbl.items():
            if op == "~=":
                if str(value) in str(v) or str(v) in str(value):
                    ids.append(id)
                continue

            op_func = {
                "=": operator.eq,
                "!=": operator.ne,
                ">=": operator.ge,
                "<=": operator.le,
                ">": operator.gt,
                "<": operator.lt,
            }[op]

            if op_func(v, value):
                ids.append(id)
        return ids
