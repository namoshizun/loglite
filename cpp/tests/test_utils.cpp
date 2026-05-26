#include <gtest/gtest.h>

#include "utils.hpp"
#include "handlers/common.hpp"

#include <thread>

using namespace loglite;
using namespace loglite::handlers;

// ── Timer ────────────────────────────────────────────────────────────────────

TEST(UtilsTest, TimerElapsedMsPositive) {
    Timer t;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_GE(t.elapsed_ms(), 40.0);
}

TEST(UtilsTest, TimerElapsedS) {
    Timer t;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_GE(t.elapsed_s(), 0.04);
}

// ── url_decode ───────────────────────────────────────────────────────────────

TEST(UtilsTest, UrlDecodeNoEncoding) { EXPECT_EQ(url_decode("hello"), "hello"); }

TEST(UtilsTest, UrlDecodePlus) { EXPECT_EQ(url_decode("hello+world"), "hello world"); }

TEST(UtilsTest, UrlDecodePercent) {
    EXPECT_EQ(url_decode("hello%20world"), "hello world");
    EXPECT_EQ(url_decode("%3C%3E"), "<>");
}

TEST(UtilsTest, UrlDecodeInvalidHex) { EXPECT_EQ(url_decode("test%GG"), "test"); }

TEST(UtilsTest, UrlDecodeEmpty) { EXPECT_EQ(url_decode(""), ""); }

// ── strip_spaces ─────────────────────────────────────────────────────────────

TEST(UtilsTest, StripSpacesTrimsEnds) {
    EXPECT_EQ(strip_spaces("  query_avg  "), "query_avg");
    EXPECT_EQ(strip_spaces("\tfoo\n"), "foo");
}

TEST(UtilsTest, StripSpacesAllBlank) { EXPECT_TRUE(strip_spaces(" \t ").empty()); }

TEST(UtilsTest, StripSpacesEmpty) { EXPECT_TRUE(strip_spaces("").empty()); }

// ── ParseIntParam ────────────────────────────────────────────────────────────

TEST(UtilsTest, ParseIntParamValid) {
    EXPECT_EQ(ParseIntParam("42"), std::optional(42));
    EXPECT_EQ(ParseIntParam("-1"), std::optional(-1));
    EXPECT_EQ(ParseIntParam("0"), std::optional(0));
}

TEST(UtilsTest, ParseIntParamEmpty) { EXPECT_EQ(ParseIntParam(""), std::nullopt); }

TEST(UtilsTest, ParseIntParamNonNumeric) {
    EXPECT_EQ(ParseIntParam("abc"), std::nullopt);
    EXPECT_EQ(ParseIntParam("12a"), std::nullopt);
}

TEST(UtilsTest, ParseIntParamOverflow) {
    // Value that exceeds int range
    EXPECT_EQ(ParseIntParam("9999999999999999999"), std::nullopt);
}

// ── bytes_to_mb ──────────────────────────────────────────────────────────────

TEST(UtilsTest, BytesToMb) {
    EXPECT_DOUBLE_EQ(bytes_to_mb(1048576), 1.0);
    EXPECT_DOUBLE_EQ(bytes_to_mb(0), 0.0);
}

// ── parse_iso8601 / format_utc ──────────────────────────────────────────────

TEST(UtilsTest, ParseIso8601WithZRoundTripsViaFormatUtc) {
    auto tp = parse_iso8601("2024-06-15T08:30:00Z");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(format_utc(*tp), "2024-06-15T08:30:00.000Z");
}

TEST(UtilsTest, ParseIso8601WithoutZ) {
    auto tp = parse_iso8601("2024-01-01T00:00:00");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(format_utc(*tp), "2024-01-01T00:00:00.000Z");
}

TEST(UtilsTest, FormatUtcIncludesMilliseconds) {
    auto tp = parse_iso8601("2024-01-01T12:34:56.789Z");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(format_utc(*tp), "2024-01-01T12:34:56.789Z");
}

TEST(UtilsTest, ParseIso8601FractionalPreservesSubseconds) {
    auto whole = parse_iso8601("2024-01-01T12:34:56Z");
    auto frac = parse_iso8601("2024-01-01T12:34:56.999999Z");
    ASSERT_TRUE(whole.has_value());
    ASSERT_TRUE(frac.has_value());
    EXPECT_NE(*whole, *frac);
    EXPECT_LT(*whole, *frac);
}

TEST(UtilsTest, ParseIso8601NumericOffsetColoned) {
    auto utc = parse_iso8601("2023-12-31T16:00:00Z");
    auto east = parse_iso8601("2024-01-01T00:00:00+08:00");
    ASSERT_TRUE(utc.has_value());
    ASSERT_TRUE(east.has_value());
    EXPECT_EQ(*utc, *east);
}

TEST(UtilsTest, ParseIso8601NumericOffsetCompact) {
    auto tp = parse_iso8601("2024-01-01T00:00:30+0030");
    auto expected = parse_iso8601("2023-12-31T23:30:30Z");
    ASSERT_TRUE(tp.has_value());
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(*tp, *expected);
}

TEST(UtilsTest, ParseIso8601DateOnlyRejected) {
    EXPECT_EQ(parse_iso8601("2024-01-01"), std::nullopt);
}

TEST(UtilsTest, ParseIso8601InvalidRejected) {
    EXPECT_EQ(parse_iso8601(""), std::nullopt);
    EXPECT_EQ(parse_iso8601("not-a-time"), std::nullopt);
    EXPECT_EQ(parse_iso8601("2024-13-40T99:99:99Z"), std::nullopt);
}

// ── SplitURLTarget ───────────────────────────────────────────────────────────

TEST(UtilsTest, SplitTargetWithParams) {
    auto [path, qs] = SplitURLTarget("/logs/query?fields=*&limit=10");
    EXPECT_EQ(path, "/logs/query");
    EXPECT_EQ(qs, "fields=*&limit=10");
}

TEST(UtilsTest, SplitTargetOnlyPath) {
    auto [path, qs] = SplitURLTarget("/simple");
    EXPECT_EQ(path, "/simple");
    EXPECT_TRUE(qs.empty());
}

TEST(UtilsTest, SplitTargetEmpty) {
    auto [path, qs] = SplitURLTarget("");
    EXPECT_EQ(path, "");
    EXPECT_TRUE(qs.empty());
}

// ── ParseQueryString ─────────────────────────────────────────────────────────

TEST(UtilsTest, ParseQueryStringEmpty) {
    auto params = ParseQueryString("");
    EXPECT_TRUE(params.empty());
}

TEST(UtilsTest, ParseQueryStringSingleParam) {
    auto params = ParseQueryString("key=value");
    EXPECT_EQ(params.find("key")->second, "value");
}

TEST(UtilsTest, ParseQueryStringMultipleValuesSameKey) {
    auto params = ParseQueryString("key=val1&key=val2");
    auto range = params.equal_range("key");
    int count = 0;
    for (auto it = range.first; it != range.second; ++it) ++count;
    EXPECT_EQ(count, 2);
}

TEST(UtilsTest, ParseQueryStringNoEquals) {
    auto params = ParseQueryString("bareword");
    EXPECT_TRUE(params.empty());
}

// ── log functions ───────────────────────────────────────────────────────────

#include "log.hpp"

TEST(UtilsTest, LogDebugEnabled) {
    log::SetLevel(log::Level::kDebug);
    log::DEBUG("test message");
    SUCCEED();
}

TEST(UtilsTest, LogDebugDisabled) {
    log::SetLevel(log::Level::kInfo);
    log::DEBUG("hidden message");
    SUCCEED();
}

TEST(UtilsTest, LogInfo) {
    log::INFO("test info");
    SUCCEED();
}

TEST(UtilsTest, LogWarn) {
    log::WARN("test warning");
    SUCCEED();
}

TEST(UtilsTest, LogError) {
    log::ERROR("test error");
    SUCCEED();
}
