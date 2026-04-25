#include <gtest/gtest.h>

#include "handlers/common.hpp"
#include "column_dict.hpp"

using namespace loglite;
using namespace loglite::handlers;

// ── Filter expression parsing ─────────────────────────────────────────────────

TEST(FilterParseTest, EqualOperator) {
    auto filters = parse_filter_expr("level", "=ERROR");
    ASSERT_EQ(filters.size(), 1u);
    EXPECT_EQ(filters[0].field, "level");
    EXPECT_EQ(filters[0].op,    "=");
    EXPECT_EQ(filters[0].value.get<std::string>(), "ERROR");
}

TEST(FilterParseTest, MultipleOperators) {
    auto filters = parse_filter_expr("timestamp",
        ">=2024-01-01T00:00:00,<=2024-01-02T00:00:00");
    ASSERT_EQ(filters.size(), 2u);
    EXPECT_EQ(filters[0].op, ">=");
    EXPECT_EQ(filters[1].op, "<=");
}

TEST(FilterParseTest, SubstringOperator) {
    auto filters = parse_filter_expr("message", "~=timeout");
    ASSERT_EQ(filters.size(), 1u);
    EXPECT_EQ(filters[0].op, "~=");
    EXPECT_EQ(filters[0].value.get<std::string>(), "timeout");
}

TEST(FilterParseTest, NotEqualOperator) {
    auto filters = parse_filter_expr("level", "!=DEBUG");
    ASSERT_EQ(filters.size(), 1u);
    EXPECT_EQ(filters[0].op, "!=");
}

TEST(FilterParseTest, EmptyExpressionReturnsEmpty) {
    auto filters = parse_filter_expr("level", "");
    EXPECT_TRUE(filters.empty());
}

// ── Query-string parsing ──────────────────────────────────────────────────────

TEST(QueryStringTest, BasicParsing) {
    auto params = parse_query_string("fields=*&limit=100&offset=0");
    EXPECT_EQ(params.find("fields")->second, "*");
    EXPECT_EQ(params.find("limit")->second,  "100");
    EXPECT_EQ(params.find("offset")->second, "0");
}

TEST(QueryStringTest, FilterParam) {
    auto params = parse_query_string("fields=*&limit=10&offset=0&level==ERROR");
    auto it = params.find("level");
    ASSERT_NE(it, params.end());
    EXPECT_EQ(it->second, "=ERROR");
}

TEST(QueryStringTest, SplitTarget) {
    auto [path, qs] = split_target("/logs?fields=*&limit=10");
    EXPECT_EQ(path, "/logs");
    EXPECT_EQ(qs,   "fields=*&limit=10");
}

TEST(QueryStringTest, SplitTargetNoQuery) {
    auto [path, qs] = split_target("/health");
    EXPECT_EQ(path, "/health");
    EXPECT_TRUE(qs.empty());
}

// ── ColumnDictionary ──────────────────────────────────────────────────────────

TEST(ColumnDictTest, GetOrCreate) {
    LookupTable lut;
    std::vector<std::tuple<std::string, std::string, int>> persisted;

    ColumnDictionary dict{lut, [&](const std::string& c, const std::string& v, int id) {
        persisted.emplace_back(c, v, id);
    }};

    auto id1 = dict.get_or_create("level", "INFO");
    auto id2 = dict.get_or_create("level", "ERROR");
    auto id3 = dict.get_or_create("level", "INFO"); // same → same id

    EXPECT_EQ(id1, id3);
    EXPECT_NE(id1, id2);
    EXPECT_EQ(persisted.size(), 2u); // only new entries persisted
}

TEST(ColumnDictTest, GetValue) {
    LookupTable lut{{"level", {{"INFO", 1}, {"ERROR", 2}}}};
    ColumnDictionary dict{lut, nullptr};

    EXPECT_EQ(dict.get_value("level", 1), "INFO");
    EXPECT_EQ(dict.get_value("level", 2), "ERROR");
}

TEST(ColumnDictTest, QueryCandidatesEqual) {
    LookupTable lut{{"level", {{"INFO", 1}, {"ERROR", 2}, {"WARNING", 3}}}};
    ColumnDictionary dict{lut, nullptr};

    QueryFilter f{"level", "=", "ERROR"};
    auto ids = dict.query_candidates(f);
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], 2);
}

TEST(ColumnDictTest, QueryCandidatesSubstring) {
    LookupTable lut{{"level", {{"INFO", 1}, {"ERROR", 2}, {"WARNING", 3}}}};
    ColumnDictionary dict{lut, nullptr};

    QueryFilter f{"level", "~=", "WARN"};
    auto ids = dict.query_candidates(f);
    EXPECT_EQ(ids.size(), 1u);
}

TEST(ColumnDictTest, QueryCandidatesNoMatch) {
    LookupTable lut{{"level", {{"INFO", 1}}}};
    ColumnDictionary dict{lut, nullptr};

    QueryFilter f{"level", "=", "CRITICAL"};
    auto ids = dict.query_candidates(f);
    EXPECT_TRUE(ids.empty());
}
