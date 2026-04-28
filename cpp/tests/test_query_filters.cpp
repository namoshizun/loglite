#include <gtest/gtest.h>

#include "handlers/common.hpp"
#include "column_dict.hpp"

using namespace loglite;
using namespace loglite::handlers;

// ── Filter expression parsing ─────────────────────────────────────────────────

TEST(FilterParseTest, EqualOperator) {
    auto filters = ParseQueryFilters("level", "=ERROR");
    ASSERT_EQ(filters.size(), 1u);
    EXPECT_EQ(filters[0].field, "level");
    EXPECT_EQ(filters[0].op, "=");
    EXPECT_EQ(filters[0].value.get<std::string>(), "ERROR");
}

TEST(FilterParseTest, MultipleOperators) {
    auto filters = ParseQueryFilters("timestamp", ">=2024-01-01T00:00:00,<=2024-01-02T00:00:00");
    ASSERT_EQ(filters.size(), 2u);
    EXPECT_EQ(filters[0].op, ">=");
    EXPECT_EQ(filters[1].op, "<=");
}

TEST(FilterParseTest, SubstringOperator) {
    auto filters = ParseQueryFilters("message", "~=timeout");
    ASSERT_EQ(filters.size(), 1u);
    EXPECT_EQ(filters[0].op, "~=");
    EXPECT_EQ(filters[0].value.get<std::string>(), "timeout");
}

TEST(FilterParseTest, NotEqualOperator) {
    auto filters = ParseQueryFilters("level", "!=DEBUG");
    ASSERT_EQ(filters.size(), 1u);
    EXPECT_EQ(filters[0].op, "!=");
}

TEST(FilterParseTest, EmptyExpressionReturnsEmpty) {
    auto filters = ParseQueryFilters("level", "");
    EXPECT_TRUE(filters.empty());
}

// ── Query-string parsing ──────────────────────────────────────────────────────

TEST(QueryStringTest, BasicParsing) {
    auto params = ParseQueryString("fields=*&limit=100&offset=0");
    EXPECT_EQ(params.find("fields")->second, "*");
    EXPECT_EQ(params.find("limit")->second, "100");
    EXPECT_EQ(params.find("offset")->second, "0");
}

TEST(QueryStringTest, FilterParam) {
    auto params = ParseQueryString("fields=*&limit=10&offset=0&level==ERROR");
    auto it = params.find("level");
    ASSERT_NE(it, params.end());
    EXPECT_EQ(it->second, "=ERROR");
}

TEST(QueryStringTest, SplitTarget) {
    auto [path, qs] = SplitURLTarget("/logs?fields=*&limit=10");
    EXPECT_EQ(path, "/logs");
    EXPECT_EQ(qs, "fields=*&limit=10");
}

TEST(QueryStringTest, SplitTargetNoQuery) {
    auto [path, qs] = SplitURLTarget("/health");
    EXPECT_EQ(path, "/health");
    EXPECT_TRUE(qs.empty());
}

