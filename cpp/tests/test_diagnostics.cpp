#include <gtest/gtest.h>

#include "test_support.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"
#include "metrics.hpp"
#include "tasks/diagnostics.hpp"

#include <limits>

using namespace loglite;

class DiagnosticsMetricsTest : public ::testing::Test {
   protected:
    void SetUp() override { metrics::MetricsRegistry::Instance().Reset(std::chrono::seconds{60}); }
    void TearDown() override { metrics::MetricsRegistry::Instance().Reset(); }
};

class DiagnosticsTest : public test::DatabaseFixture {
   protected:
    void SetUp() override {
        metrics::MetricsRegistry::Instance().Reset(std::chrono::seconds{60});
        DatabaseFixture::SetUp();
    }
    void TearDown() override {
        DatabaseFixture::TearDown();
        metrics::MetricsRegistry::Instance().Reset();
    }
};

// ── round_stat ──────────────────────────────────────────────────────────────

TEST(DiagnosticsSummaryTest, RoundingUsesNearestIntegerWithTiesAwayFromZero) {
    const std::pair<double, int64_t> cases[]{{3.2, 3},   {3.7, 4},   {3.5, 4}, {-3.2, -3},
                                             {-3.7, -4}, {-3.5, -4}, {0.0, 0}};
    for (const auto& [value, expected] : cases) {
        SCOPED_TRACE(value);
        EXPECT_EQ(tasks::detail::round_stat(value), expected);
    }
}

// ── summarize_observations ──────────────────────────────────────────────────

TEST(DiagnosticsSummaryTest, EmptyAndUnmatchedObservationsProduceZeroSummaries) {
    const std::vector<metrics::Observation> cases[]{
        {},
        {{std::chrono::steady_clock::now(), metrics::kIngestRequest, 100.0, 1}},
    };
    for (const auto& samples : cases) {
        SCOPED_TRACE(samples.size());
        auto [q] = tasks::detail::summarize_observations(samples, metrics::kQueryRequest);
        EXPECT_EQ(q.sample_count, 0);
        EXPECT_EQ(q.item_count, 0);
        EXPECT_DOUBLE_EQ(q.value_total, 0.0);
        EXPECT_DOUBLE_EQ(q.min, 0.0);
        EXPECT_DOUBLE_EQ(q.max, 0.0);
        EXPECT_DOUBLE_EQ(q.avg, 0.0);
    }
}

TEST(DiagnosticsSummaryTest, SummarizeMultipleMetrics) {
    std::vector<metrics::Observation> samples{
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 5.0, 1},
        {std::chrono::steady_clock::now(), metrics::kIngestRequest, 100.0, 1},
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 15.0, 1},
        {std::chrono::steady_clock::now(), metrics::kIngestRequest, 200.0, 1},
        {std::chrono::steady_clock::now(), metrics::kBacklogDrop, 0.0, 5},
        {std::chrono::steady_clock::now(), metrics::kInsertBatch, 2.5, 20},
        {std::chrono::steady_clock::now(), metrics::kInsertBatch, 5.5, 30},
        // Only requested names contribute; this sample must be ignored.
        {std::chrono::steady_clock::now(), "unrequested", 9999.0, 17},
    };

    auto [q, ingest, drops, inserts] = tasks::detail::summarize_observations(
        samples, metrics::kQueryRequest, metrics::kIngestRequest, metrics::kBacklogDrop,
        metrics::kInsertBatch);

    EXPECT_EQ(q.sample_count, 2);
    EXPECT_EQ(q.item_count, 2);
    EXPECT_DOUBLE_EQ(q.value_total, 20.0);
    EXPECT_DOUBLE_EQ(q.min, 5.0);
    EXPECT_DOUBLE_EQ(q.max, 15.0);
    EXPECT_DOUBLE_EQ(q.avg, 10.0);

    EXPECT_EQ(ingest.sample_count, 2);
    EXPECT_DOUBLE_EQ(ingest.min, 100.0);
    EXPECT_DOUBLE_EQ(ingest.max, 200.0);
    EXPECT_DOUBLE_EQ(ingest.avg, 150.0);

    EXPECT_EQ(drops.sample_count, 1);
    EXPECT_EQ(drops.item_count, 5);

    EXPECT_EQ(inserts.sample_count, 2);
    EXPECT_EQ(inserts.item_count, 50);  // 20 + 30, distinct from the number of observations
    EXPECT_DOUBLE_EQ(inserts.value_total, 8.0);
    EXPECT_DOUBLE_EQ(inserts.min, 2.5);
    EXPECT_DOUBLE_EQ(inserts.max, 5.5);
    EXPECT_DOUBLE_EQ(inserts.avg, 4.0);
}

TEST(DiagnosticsSummaryTest, SingleSamplesSetMinMaxAndAverageToTheirOwnValue) {
    for (double value : {42.0, -42.0, 0.0}) {
        SCOPED_TRACE(value);
        const std::vector<metrics::Observation> samples{
            {std::chrono::steady_clock::now(), metrics::kQueryRequest, value, 1},
        };
        auto [q] = tasks::detail::summarize_observations(samples, metrics::kQueryRequest);
        EXPECT_EQ(q.sample_count, 1);
        EXPECT_EQ(q.item_count, 1);
        EXPECT_DOUBLE_EQ(q.value_total, value);
        EXPECT_DOUBLE_EQ(q.min, value);
        EXPECT_DOUBLE_EQ(q.max, value);
        EXPECT_DOUBLE_EQ(q.avg, value);
    }
}

// ── build_activity_stats ────────────────────────────────────────────────────

TEST_F(DiagnosticsMetricsTest, BuildActivityStatsFromRealMetrics) {
    auto& registry = metrics::MetricsRegistry::Instance();

    registry.Collect(metrics::kQueryRequest, 5.0);
    registry.Collect(metrics::kQueryRequest, 15.0);
    registry.Collect(metrics::kIngestRequest, 512.0);
    registry.Collect(metrics::kIngestRequest, 1024.0);
    registry.Collect(metrics::kBacklogDrop, 0.0, 2);
    registry.Collect(metrics::kInsertBatch, 3.0, 10);
    registry.Collect(metrics::kInsertBatch, 7.0, 20);

    registry.IncrementGauge(metrics::kSseSession);
    registry.IncrementGauge(metrics::kSseSession);
    registry.IncrementGauge(metrics::kHttpConnection);

    auto samples = registry.Flush();
    auto row = tasks::detail::build_activity_stats("2024-01-01T00:00:00Z", "2024-01-01T00:01:00Z",
                                                   samples);

    EXPECT_EQ(row.since, "2024-01-01T00:00:00Z");
    EXPECT_EQ(row.until, "2024-01-01T00:01:00Z");

    EXPECT_EQ(row.query_count, 2);
    EXPECT_EQ(row.query_min, 5);
    EXPECT_EQ(row.query_max, 15);
    EXPECT_EQ(row.query_avg, 10);

    EXPECT_EQ(row.ingest_count, 2);
    EXPECT_EQ(row.ingest_size_min, 512);
    EXPECT_EQ(row.ingest_size_max, 1024);
    EXPECT_EQ(row.ingest_size_avg, 768);

    EXPECT_EQ(row.ingest_drop_count, 2);

    EXPECT_EQ(row.insert_batch_count, 2);
    EXPECT_EQ(row.insert_total_count, 30);
    EXPECT_EQ(row.insert_total_cost, 10);

    EXPECT_EQ(row.sse_session_count, 2);
    EXPECT_EQ(row.http_conn_count, 1);
}

TEST_F(DiagnosticsMetricsTest, BuildActivityStatsEmptySamples) {
    auto& registry = metrics::MetricsRegistry::Instance();

    registry.IncrementGauge(metrics::kSseSession);

    auto samples = registry.Flush();
    auto row = tasks::detail::build_activity_stats("2024-01-01T00:00:00Z", "2024-01-01T00:01:00Z",
                                                   samples);

    EXPECT_EQ(row.query_count, 0);
    EXPECT_EQ(row.query_min, 0);
    EXPECT_EQ(row.query_max, 0);
    EXPECT_EQ(row.query_avg, 0);
    EXPECT_EQ(row.ingest_count, 0);
    EXPECT_EQ(row.ingest_drop_count, 0);
    EXPECT_EQ(row.insert_batch_count, 0);
    EXPECT_EQ(row.insert_total_count, 0);
    EXPECT_EQ(row.sse_session_count, 1);
    EXPECT_EQ(row.http_conn_count, 0);
}

// ── Persistence: InsertActivityStats, InsertDatabaseStats, DeleteStatsBefore ─

TEST_F(DiagnosticsTest, DeleteStatsBeforePrunes) {
    ActivityStatsRow old_row;
    old_row.since = "2024-01-01T00:00:00Z";
    old_row.until = "2024-01-01T00:01:00Z";
    old_row.query_count = 1;

    ActivityStatsRow new_row;
    new_row.since = "2024-01-02T00:00:00Z";
    new_row.until = "2024-01-02T00:01:00Z";
    new_row.query_count = 2;

    EXPECT_TRUE(db_->InsertActivityStats(old_row));
    EXPECT_TRUE(db_->InsertActivityStats(new_row));
    EXPECT_TRUE(db_->InsertDatabaseStats({"2024-01-01T00:01:00Z", 10, 4096}));
    EXPECT_TRUE(db_->InsertDatabaseStats({"2024-01-02T00:01:00Z", 20, 8192}));

    // Equal timestamps survive the cutoff; only strictly older rows are pruned.
    EXPECT_EQ(db_->DeleteStatsBefore("2024-01-01T00:01:00Z"), 0);
    int removed = db_->DeleteStatsBefore("2024-01-02T00:00:00Z");
    EXPECT_EQ(removed, 2);
    EXPECT_EQ(
        reader_->QueryActivityStats(old_row.since, new_row.until, {"query_count"}, "asc").data,
        (std::vector<std::vector<nlohmann::json>>{{2}}));
    EXPECT_EQ(reader_->QueryDatabaseStats(old_row.since, new_row.until, {"rows_count"}, "asc").data,
              (std::vector<std::vector<nlohmann::json>>{{20}}));

    removed = db_->DeleteStatsBefore("2024-01-03T00:00:00Z");
    EXPECT_EQ(removed, 2);
}

TEST_F(DiagnosticsTest, ActivityStatsRoundTripEveryPersistedField) {
    const ActivityStatsRow row{
        .since = "2024-06-01T00:00:00Z",
        .until = "2024-06-01T00:01:00Z",
        .query_count = 42,
        .query_min = 2,
        .query_max = 98,
        .query_avg = 25,
        .ingest_count = 100,
        .ingest_size_min = 64,
        .ingest_size_max = 8192,
        .ingest_size_avg = 2048,
        .ingest_drop_count = 5,
        .insert_batch_count = 10,
        .insert_total_count = 1000,
        .insert_total_cost = 500,
        .sse_session_count = 7,
        .http_conn_count = 3,
    };
    ASSERT_TRUE(db_->InsertActivityStats(row));
    auto result = reader_->QueryActivityStats(row.since, row.until, {"*"}, "desc");
    EXPECT_EQ(result.fields,
              (std::vector<std::string>{
                  "id", "since", "until", "query_count", "query_min", "query_max", "query_avg",
                  "ingest_count", "ingest_size_min", "ingest_size_max", "ingest_size_avg",
                  "ingest_drop_count", "insert_batch_count", "insert_total_count",
                  "insert_total_cost", "sse_session_count", "http_conn_count"}));
    EXPECT_EQ(
        result.data,
        (std::vector<std::vector<nlohmann::json>>{{1, row.since, row.until, 42, 2, 98, 25, 100, 64,
                                                   8192, 2048, 5, 10, 1000, 500, 7, 3}}));
}

// ── End-to-end: metrics → stats row → persistence ──────────────────────────

TEST_F(DiagnosticsTest, EndToEndMetricsToPersistence) {
    auto& registry = metrics::MetricsRegistry::Instance();

    registry.Collect(metrics::kQueryRequest, 12.0);
    registry.Collect(metrics::kIngestRequest, 256.0);
    registry.Collect(metrics::kIngestRequest, 512.0);
    registry.Collect(metrics::kInsertBatch, 4.5, 15);
    registry.IncrementGauge(metrics::kSseSession);
    registry.IncrementGauge(metrics::kSseSession);

    auto samples = registry.Flush();
    auto row = tasks::detail::build_activity_stats("2025-01-01T10:00:00Z", "2025-01-01T10:01:00Z",
                                                   samples);

    EXPECT_TRUE(db_->InsertActivityStats(row));
    const auto sampled_size = db_->GetSizeBytes();
    EXPECT_TRUE(db_->InsertDatabaseStats({row.until, db_->EstimateLogRowCount(), sampled_size}));

    const auto persisted = reader_->QueryActivityStats(
        row.since, row.until,
        {"query_count", "ingest_count", "ingest_size_avg", "insert_total_count",
         "insert_total_cost", "sse_session_count"},
        "asc");
    EXPECT_EQ(persisted.data, (std::vector<std::vector<nlohmann::json>>{{1, 2, 384, 15, 5, 2}}));
    const auto database =
        reader_->QueryDatabaseStats(row.since, row.until, {"rows_count", "db_size"}, "asc");
    EXPECT_EQ(database.data, (std::vector<std::vector<nlohmann::json>>{{0, sampled_size}}));
}

// ── Extreme values must not break min/max initialization ──────────────────

TEST(DiagnosticsSummaryTest, SummarizeObservationMinMaxWithFloatExtremes) {
    auto now = std::chrono::steady_clock::now();
    std::vector<metrics::Observation> samples{
        {now, metrics::kQueryRequest, std::numeric_limits<double>::max(), 1},
        {now, metrics::kQueryRequest, std::numeric_limits<double>::lowest(), 1},
    };

    auto [q] = tasks::detail::summarize_observations(samples, metrics::kQueryRequest);
    EXPECT_DOUBLE_EQ(q.min, std::numeric_limits<double>::lowest());
    EXPECT_DOUBLE_EQ(q.max, std::numeric_limits<double>::max());
    EXPECT_EQ(q.sample_count, 2);
}

// ── Stats query methods ────────────────────────────────────────────────────

TEST_F(DiagnosticsTest, StatsQueriesRejectUnknownFields) {
    EXPECT_THROW(reader_->QueryActivityStats("2024-01-01T00:00:00Z", "2024-01-01T00:01:00Z",
                                             {"no_such_field"}, "desc"),
                 std::runtime_error);
    EXPECT_THROW(reader_->QueryDatabaseStats("2024-01-01T00:00:00Z", "2024-01-01T00:01:00Z",
                                             {"bad_column"}, "desc"),
                 std::runtime_error);
}

TEST_F(DiagnosticsTest, StatsWindowsIncludeBoundariesAndPreserveOrdering) {
    const std::vector<std::string> timestamps{"2024-01-01T00:01:00Z", "2024-01-01T00:02:00Z",
                                              "2024-01-02T00:01:00Z"};
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(db_->InsertActivityStats(
            {.since = "2024-01-01T00:00:00Z", .until = timestamps[i], .query_count = i + 1}));
        ASSERT_TRUE(db_->InsertDatabaseStats({timestamps[i], i + 1, 4096}));
    }
    struct Case {
        const char* since;
        const char* until;
        const char* ordering;
        std::vector<std::vector<nlohmann::json>> data;
    };
    const Case cases[]{
        {"2024-01-01T00:01:00Z", "2024-01-01T00:02:00Z", "asc", {{1}, {2}}},
        {"2024-01-01T00:01:00Z", "2024-01-01T00:02:00Z", "desc", {{2}, {1}}},
        {"2024-01-01T00:01:00Z", "2024-01-01T00:01:00Z", "asc", {{1}}},
        {"2025-01-01T00:00:00Z", "2025-01-01T01:00:00Z", "desc", {}},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(std::string{c.since} + c.ordering);
        const auto activity =
            reader_->QueryActivityStats(c.since, c.until, {"query_count"}, c.ordering);
        EXPECT_EQ(activity.fields, (std::vector<std::string>{"query_count"}));
        EXPECT_EQ(activity.data, c.data);
        const auto database =
            reader_->QueryDatabaseStats(c.since, c.until, {"rows_count"}, c.ordering);
        EXPECT_EQ(database.fields, (std::vector<std::string>{"rows_count"}));
        EXPECT_EQ(database.data, c.data);
    }
}

TEST_F(DiagnosticsTest, DatabaseStatsRoundTripWildcardAndProjectedFields) {
    const DatabaseStatsRow row{"2024-01-01T00:00:00Z", 100, 4096};
    ASSERT_TRUE(db_->InsertDatabaseStats(row));
    const auto all =
        reader_->QueryDatabaseStats(row.timestamp, "2024-01-01T00:01:00Z", {"*"}, "desc");
    EXPECT_EQ(all.fields, (std::vector<std::string>{"id", "timestamp", "rows_count", "db_size"}));
    EXPECT_EQ(all.data, (std::vector<std::vector<nlohmann::json>>{{1, row.timestamp, 100, 4096}}));
    const auto projected = reader_->QueryDatabaseStats(row.timestamp, "2024-01-01T00:01:00Z",
                                                       {"db_size", "rows_count"}, "desc");
    EXPECT_EQ(projected.fields, (std::vector<std::string>{"db_size", "rows_count"}));
    EXPECT_EQ(projected.data, (std::vector<std::vector<nlohmann::json>>{{4096, 100}}));
}
