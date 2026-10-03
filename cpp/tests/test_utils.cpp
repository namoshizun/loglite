#include <gtest/gtest.h>

#include "utils.hpp"
#include "handlers/common.hpp"

#include <algorithm>
#include <limits>

using namespace loglite;
using namespace loglite::handlers;

// ── URL decoding and whitespace ───────────────────────────────────────────────

TEST(UtilsTest, UrlDecodingPreservesEncodingSemantics) {
    const std::pair<std::string_view, std::string_view> cases[]{
        {"hello", "hello"}, {"hello+world", "hello world"}, {"hello%20world", "hello world"},
        {"%3C%3E", "<>"},   {"%2B08%3A00", "+08:00"},       {"test%GG", "test"},
        {"", ""},
    };
    for (const auto& [encoded, expected] : cases) {
        SCOPED_TRACE(encoded);
        EXPECT_EQ(url_decode(encoded), expected);
    }
}

TEST(UtilsTest, StripSpacesPreservesInteriorWhitespace) {
    for (const auto& [raw, expected] : {std::pair{"  query_avg  ", "query_avg"},
                                        {"\tfoo\n", "foo"},
                                        {" a b ", "a b"},
                                        {" \t ", ""},
                                        {"", ""}}) {
        SCOPED_TRACE(raw);
        EXPECT_EQ(strip_spaces(raw), expected);
    }
}

// ── Numeric parsing ───────────────────────────────────────────────────────────

TEST(UtilsTest, IntegerParsingRejectsPartialAndOutOfRangeValues) {
    for (int value :
         {0, -1, 42, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        EXPECT_EQ(ParseIntParam(std::to_string(value)), value);
    }
    for (const auto* raw :
         {"", "abc", "12a", "+4", " 4", "2147483648", "-2147483649", "9999999999999999999"}) {
        SCOPED_TRACE(raw);
        EXPECT_EQ(ParseIntParam(raw), std::nullopt);
    }
}

TEST(UtilsTest, SizeParsingUsesBinaryUnitsAndRejectsInvalidInput) {
    const std::pair<std::string_view, int64_t> cases[]{
        {"1KB", 1024},
        {"1MB", 1024 * 1024},
        {"1GB", 1024LL * 1024 * 1024},
        {"2TB", 2LL * 1024 * 1024 * 1024 * 1024},
        {"500MB", 500LL * 1024 * 1024},
    };
    for (const auto& [raw, bytes] : cases) {
        SCOPED_TRACE(raw);
        EXPECT_EQ(parse_size_to_bytes(raw), bytes);
    }
    for (const auto* raw : {"bad", ""}) EXPECT_THROW(parse_size_to_bytes(raw), std::exception);
}

// ── parse_iso8601 / format_utc ─────────────────────────────────────────────────

TEST(UtilsTest, Iso8601NormalizesOffsetsAndPreservesFractionalSeconds) {
    const std::pair<std::string_view, std::string_view> cases[]{
        {"2024-06-15T08:30:00Z", "2024-06-15T08:30:00.000Z"},
        {"2024-01-01T00:00:00", "2024-01-01T00:00:00.000Z"},
        {"2024-01-01T12:34:56.789Z", "2024-01-01T12:34:56.789Z"},
        {"2024-01-01T00:00:00+08:00", "2023-12-31T16:00:00.000Z"},
        {"2024-01-01T00:00:30+0030", "2023-12-31T23:30:30.000Z"},
        {"2024-01-01T00:00:00.500-05:00", "2024-01-01T05:00:00.500Z"},
    };
    for (const auto& [raw, utc] : cases) {
        SCOPED_TRACE(raw);
        auto parsed = parse_iso8601(raw);
        ASSERT_TRUE(parsed);
        EXPECT_EQ(format_utc(*parsed), utc);
    }
    // Sub-millisecond precision must survive parsing even though formatting truncates it.
    const auto whole = parse_iso8601("2024-01-01T12:34:56Z");
    const auto fractional = parse_iso8601("2024-01-01T12:34:56.999999Z");
    ASSERT_TRUE(whole);
    ASSERT_TRUE(fractional);
    EXPECT_EQ(*fractional - *whole, std::chrono::microseconds{999999});
}

TEST(UtilsTest, Iso8601RejectsMalformedTimestamps) {
    for (const auto* raw : {"", "not-a-time", "2024-01-01", "2024-13-40T99:99:99Z"}) {
        SCOPED_TRACE(raw);
        EXPECT_EQ(parse_iso8601(raw), std::nullopt);
    }
}

// ── SplitURLTarget / ParseQueryString ──────────────────────────────────────────

TEST(UtilsTest, SplitTargetSeparatesPathFromQuery) {
    struct Case {
        std::string_view target, path, query;
    };
    for (const auto& c : {Case{"/logs?fields=*&limit=10", "/logs", "fields=*&limit=10"},
                          Case{"/health", "/health", ""}, Case{"", "", ""}}) {
        SCOPED_TRACE(c.target);
        auto [path, query] = SplitURLTarget(c.target);
        EXPECT_EQ(path, c.path);
        EXPECT_EQ(query, c.query);
    }
}

TEST(UtilsTest, QueryStringPreservesRepeatedKeysAndDecodesValues) {
    auto params = ParseQueryString(
        "fields=*&limit=100&offset=0&level==ERROR&level=!=DEBUG"
        "&zone=%2B08%3A00&bareword");
    EXPECT_EQ(params.count("level"), 2u);
    EXPECT_EQ(params.find("fields")->second, "*");
    EXPECT_EQ(params.find("limit")->second, "100");
    EXPECT_EQ(params.find("offset")->second, "0");
    EXPECT_EQ(params.find("zone")->second, "+08:00");
    auto [first, last] = params.equal_range("level");
    std::vector<std::string> values;
    for (; first != last; ++first) values.push_back(first->second);
    std::ranges::sort(values);
    EXPECT_EQ(values, (std::vector<std::string>{"!=DEBUG", "=ERROR"}));
    EXPECT_FALSE(params.contains("bareword"));
    EXPECT_TRUE(ParseQueryString("").empty());
}
