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

// ── StatsTracker ─────────────────────────────────────────────────────────────

TEST(UtilsTest, StatsTrackerSingleCollect) {
    StatsTracker st;
    st.collect(1, 10.0);
    auto snap = st.get_and_reset();
    EXPECT_EQ(snap.count, 1);
    EXPECT_DOUBLE_EQ(snap.total_ms, 10.0);
    EXPECT_DOUBLE_EQ(snap.avg_ms, 10.0);
    EXPECT_DOUBLE_EQ(snap.max_ms, 10.0);
    EXPECT_DOUBLE_EQ(snap.min_ms, 10.0);
}

TEST(UtilsTest, StatsTrackerMultipleCollects) {
    StatsTracker st;
    st.collect(1, 100.0);
    st.collect(2, 200.0);
    auto snap = st.get_and_reset();
    EXPECT_EQ(snap.count, 3);
    EXPECT_DOUBLE_EQ(snap.total_ms, 300.0);
    EXPECT_DOUBLE_EQ(snap.avg_ms, 100.0);
    EXPECT_DOUBLE_EQ(snap.max_ms, 200.0);
    EXPECT_DOUBLE_EQ(snap.min_ms, 100.0);
}

TEST(UtilsTest, StatsTrackerResetAfterGet) {
    StatsTracker st;
    st.collect(5, 50.0);
    st.get_and_reset();
    auto snap = st.get_and_reset();
    EXPECT_EQ(snap.count, 0);
    EXPECT_DOUBLE_EQ(snap.total_ms, 0.0);
    EXPECT_DOUBLE_EQ(snap.max_ms, 0.0);
    EXPECT_DOUBLE_EQ(snap.min_ms, 0.0);
}

TEST(UtilsTest, StatsTrackerMinAfterReset) {
    StatsTracker st;
    st.collect(1, 42.0);
    st.get_and_reset();
    auto snap = st.get_and_reset();
    EXPECT_DOUBLE_EQ(snap.min_ms, 0.0);
}

// ── url_decode ───────────────────────────────────────────────────────────────

TEST(UtilsTest, UrlDecodeNoEncoding) {
    EXPECT_EQ(url_decode("hello"), "hello");
}

TEST(UtilsTest, UrlDecodePlus) {
    EXPECT_EQ(url_decode("hello+world"), "hello world");
}

TEST(UtilsTest, UrlDecodePercent) {
    EXPECT_EQ(url_decode("hello%20world"), "hello world");
    EXPECT_EQ(url_decode("%3C%3E"), "<>");
}

TEST(UtilsTest, UrlDecodeInvalidHex) {
    EXPECT_EQ(url_decode("test%GG"), "test");
}

TEST(UtilsTest, UrlDecodeEmpty) {
    EXPECT_EQ(url_decode(""), "");
}

// ── ParseIntParam ────────────────────────────────────────────────────────────

TEST(UtilsTest, ParseIntParamValid) {
    EXPECT_EQ(ParseIntParam("42"), std::optional(42));
    EXPECT_EQ(ParseIntParam("-1"), std::optional(-1));
    EXPECT_EQ(ParseIntParam("0"), std::optional(0));
}

TEST(UtilsTest, ParseIntParamEmpty) {
    EXPECT_EQ(ParseIntParam(""), std::nullopt);
}

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
    // Should not crash
    log::debug("test message", true);
    SUCCEED();
}

TEST(UtilsTest, LogDebugDisabled) {
    // Should not print
    log::debug("hidden message", false);
    SUCCEED();
}

TEST(UtilsTest, LogInfo) {
    log::info("test info");
    SUCCEED();
}

TEST(UtilsTest, LogWarn) {
    log::warn("test warning");
    SUCCEED();
}

TEST(UtilsTest, LogError) {
    log::error("test error");
    SUCCEED();
}
