from __future__ import annotations

import sys
from typing import Any
from loguru import logger
from loglite.database import Database
from loglite.utils import bytes_to_mb

ColumnName = str
ColumnValue = Any
ColumnValueId = int

LookupTable = dict[ColumnName, dict[ColumnValue, ColumnValueId]]


class ColumnDictionary:
    def __init__(self, db: Database, lookup: LookupTable):
        self.db = db
        self.__lookup: LookupTable = lookup

    @classmethod
    async def load(cls, db: Database) -> "ColumnDictionary":
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

    async def get_or_create(self, col: ColumnName, value: ColumnValue) -> ColumnValueId:
        if col not in self.__lookup:
            # Create a new column value entry
            value_id = 1
            await self.db.insert_column_dict_value(col, value, value_id)
            self.__lookup[col] = {value: value_id}

        if value_id := self.__lookup[col].get(value):
            return value_id

        # This is a new value for an existing column, so we need to create a new entry
        # and update the lookup table
        max_id = max(self.__lookup[col].values())
        value_id = max_id + 1
        self.__lookup[col][value] = value_id
        await self.db.insert_column_dict_value(col, value, value_id)

        return value_id
