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


QueryOperator = Literal["=", "!=", ">", ">=", "<", "<=", "~="]


class QueryFilter(TypedDict):
    field: str
    operator: QueryOperator
    value: Any


class PaginatedQueryResult(TypedDict):
    total: int
    offset: int
    limit: int
    results: list[dict[str, Any]]
