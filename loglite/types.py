from typing import Any, Literal, TypedDict

Query = str


class Migration(TypedDict):
    version: int
    rollout: list[Query]
    rollback: list[Query]


FieldType = Literal["INTEGER", "TEXT", "REAL", "BLOB", "DATETIME", "JSON"]


class Column(TypedDict):
    name: str
    type: FieldType
    not_null: bool
    default: Any
    primary_key: bool


class QueryFilter(TypedDict):
    field: str
    operator: Literal["=", "!=", ">", ">=", "<", "<=", "~="]
    value: Any
