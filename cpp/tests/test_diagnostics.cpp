#include <gtest/gtest.h>

#include "config.hpp"
#include "database.hpp"
#include "metrics.hpp"
#include "tasks/diagnostics.hpp"

#include <filesystem>
#include <limits>

namespace fs = std::filesystem;
using namespace loglite;

class DiagnosticsTest : public ::testing::Test {
   protected:
    void SetUp() override {
        metrics::MetricsRegistry::Instance().Reset(std::chrono::seconds{60});
        tmp_ = fs::temp_directory_path() / "loglite_diag_test";
        fs::remove_all(tmp_);
        fs::create_directories(tmp_);
    }

    void TearDown() override {
        metrics::MetricsRegistry::Instance().Reset();
        if (db_) db_.reset();
        fs::remove_all(tmp_);
    }

    void init_db() {
        cfg_.sqlite_dir = tmp_;
        cfg_.db_path = tmp_ / "logs.db";
        cfg_.log_table_name = "TestLog";
        cfg_.log_timestamp_field = "timestamp";
        cfg_.auto_rollout = true;
        cfg_.compression = {false, {}};
        cfg_.stats_retention_hours = 24;

        Migration m;
        m.version = 1;
        m.rollout = {
            "CREATE TABLE IF NOT EXISTS TestLog ("
            "  id        INTEGER PRIMARY KEY,"
            "  timestamp TEXT    NOT NULL,"
            "  message   TEXT    NOT NULL,"
            "  level     TEXT    NOT NULL"
            ")"};
        m.rollback = {"DROP TABLE IF EXISTS TestLog"};
        cfg_.migrations.push_back(m);

        db_ = std::make_unique<Database>(cfg_);
        db_->Open();
        db_->Initialize();
    }

    fs::path tmp_;
    Config cfg_;
    std::unique_ptr<Database> db_;
};

// ── round_stat ──────────────────────────────────────────────────────────────

TEST_F(DiagnosticsTest, RoundStatPositive) {
    EXPECT_EQ(tasks::detail::round_stat(3.2), 3);
    EXPECT_EQ(tasks::detail::round_stat(3.7), 4);
    EXPECT_EQ(tasks::detail::round_stat(3.5), 4);
}

TEST_F(DiagnosticsTest, RoundStatNegative) {
    EXPECT_EQ(tasks::detail::round_stat(-3.2), -3);
    EXPECT_EQ(tasks::detail::round_stat(-3.7), -4);
    EXPECT_EQ(tasks::detail::round_stat(-3.5), -4);
}

TEST_F(DiagnosticsTest, RoundStatZero) { EXPECT_EQ(tasks::detail::round_stat(0.0), 0); }

// ── summarize_observations ──────────────────────────────────────────────────

TEST_F(DiagnosticsTest, SummarizeSingleMetric) {
    std::vector<metrics::Observation> samples{
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 10.0, 1},
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 20.0, 1},
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 30.0, 1},
    };

    auto [q] = tasks::detail::summarize_observations(samples, metrics::kQueryRequest);
    EXPECT_EQ(q.sample_count, 3);
    EXPECT_EQ(q.item_count, 3);
    EXPECT_DOUBLE_EQ(q.value_total, 60.0);
    EXPECT_DOUBLE_EQ(q.min, 10.0);
    EXPECT_DOUBLE_EQ(q.max, 30.0);
    EXPECT_DOUBLE_EQ(q.avg, 20.0);
}

TEST_F(DiagnosticsTest, SummarizeEmptySamples) {
    std::vector<metrics::Observation> samples;
    auto [q] = tasks::detail::summarize_observations(samples, metrics::kQueryRequest);
    EXPECT_EQ(q.sample_count, 0);
    EXPECT_EQ(q.item_count, 0);
    EXPECT_DOUBLE_EQ(q.value_total, 0.0);
    EXPECT_DOUBLE_EQ(q.min, 0.0);
    EXPECT_DOUBLE_EQ(q.max, 0.0);
    EXPECT_DOUBLE_EQ(q.avg, 0.0);
}

TEST_F(DiagnosticsTest, SummarizeMultipleMetrics) {
    std::vector<metrics::Observation> samples{
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 5.0, 1},
        {std::chrono::steady_clock::now(), metrics::kIngestRequest, 100.0, 1},
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 15.0, 1},
        {std::chrono::steady_clock::now(), metrics::kIngestRequest, 200.0, 1},
        {std::chrono::steady_clock::now(), metrics::kBacklogDrop, 0.0, 5},
        {std::chrono::steady_clock::now(), metrics::kInsertBatch, 2.5, 20},
    };

    auto [q, ingest, drops, inserts] = tasks::detail::summarize_observations(
        samples, metrics::kQueryRequest, metrics::kIngestRequest, metrics::kBacklogDrop,
        metrics::kInsertBatch);

    EXPECT_EQ(q.sample_count, 2);
    EXPECT_DOUBLE_EQ(q.min, 5.0);
    EXPECT_DOUBLE_EQ(q.max, 15.0);
    EXPECT_DOUBLE_EQ(q.avg, 10.0);

    EXPECT_EQ(ingest.sample_count, 2);
    EXPECT_DOUBLE_EQ(ingest.min, 100.0);
    EXPECT_DOUBLE_EQ(ingest.max, 200.0);
    EXPECT_DOUBLE_EQ(ingest.avg, 150.0);

    EXPECT_EQ(drops.sample_count, 1);
    EXPECT_EQ(drops.item_count, 5);

    EXPECT_EQ(inserts.sample_count, 1);
    EXPECT_EQ(inserts.item_count, 20);
    EXPECT_DOUBLE_EQ(inserts.value_total, 2.5);
}

TEST_F(DiagnosticsTest, SummarizeIgnoresUnrequestedMetrics) {
    // Only query and ingest are requested; drops should be ignored.
    std::vector<metrics::Observation> samples{
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 10.0, 1},
        {std::chrono::steady_clock::now(), metrics::kBacklogDrop, 0.0, 3},
        {std::chrono::steady_clock::now(), metrics::kIngestRequest, 50.0, 1},
    };

    auto [q, ingest] = tasks::detail::summarize_observations(samples, metrics::kQueryRequest,
                                                             metrics::kIngestRequest);

    EXPECT_EQ(q.sample_count, 1);
    EXPECT_EQ(ingest.sample_count, 1);
}

TEST_F(DiagnosticsTest, SummarizeItemCountAccumulation) {
    std::vector<metrics::Observation> samples{
        {std::chrono::steady_clock::now(), metrics::kInsertBatch, 15.0, 10},
        {std::chrono::steady_clock::now(), metrics::kInsertBatch, 25.0, 30},
    };

    auto [inserts] = tasks::detail::summarize_observations(samples, metrics::kInsertBatch);
    EXPECT_EQ(inserts.sample_count, 2);
    EXPECT_EQ(inserts.item_count, 40);  // 10 + 30
    EXPECT_DOUBLE_EQ(inserts.value_total, 40.0);
    EXPECT_DOUBLE_EQ(inserts.avg, 20.0);
}

TEST_F(DiagnosticsTest, SummarizeSingleSampleSetsMinMaxEqual) {
    std::vector<metrics::Observation> samples{
        {std::chrono::steady_clock::now(), metrics::kQueryRequest, 42.0, 1},
    };

    auto [q] = tasks::detail::summarize_observations(samples, metrics::kQueryRequest);
    EXPECT_DOUBLE_EQ(q.min, 42.0);
    EXPECT_DOUBLE_EQ(q.max, 42.0);
    EXPECT_DOUBLE_EQ(q.avg, 42.0);
}

// ── build_activity_stats ────────────────────────────────────────────────────

TEST_F(DiagnosticsTest, BuildActivityStatsFromRealMetrics) {
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

TEST_F(DiagnosticsTest, BuildActivityStatsEmptySamples) {
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

TEST_F(DiagnosticsTest, InsertAndQueryActivityStats) {
    init_db();

    ActivityStatsRow row;
    row.since = "2024-01-01T00:00:00Z";
    row.until = "2024-01-01T00:01:00Z";
    row.query_count = 100;
    row.query_min = 1;
    row.query_max = 50;
    row.query_avg = 10;
    row.ingest_count = 30;
    row.ingest_size_min = 128;
    row.ingest_size_max = 4096;
    row.ingest_size_avg = 1024;
    row.ingest_drop_count = 2;
    row.insert_batch_count = 5;
    row.insert_total_count = 500;
    row.insert_total_cost = 250;
    row.sse_session_count = 3;
    row.http_conn_count = 1;

    EXPECT_TRUE(db_->InsertActivityStats(row));
}

TEST_F(DiagnosticsTest, InsertDatabaseStats) {
    init_db();

    DatabaseStatsRow row{"2024-01-01T00:01:00Z", 1000, 4096};
    EXPECT_TRUE(db_->InsertDatabaseStats(row));
}

TEST_F(DiagnosticsTest, InsertDatabaseStatsMultiple) {
    init_db();

    EXPECT_TRUE(db_->InsertDatabaseStats({"2024-01-01T00:01:00Z", 500, 2048}));
    EXPECT_TRUE(db_->InsertDatabaseStats({"2024-01-01T00:02:00Z", 600, 3072}));
}

TEST_F(DiagnosticsTest, DeleteStatsBeforePrunes) {
    init_db();

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

    int removed = db_->DeleteStatsBefore("2024-01-02T00:00:00Z");
    EXPECT_EQ(removed, 2);

    removed = db_->DeleteStatsBefore("2024-01-03T00:00:00Z");
    EXPECT_EQ(removed, 2);
}

TEST_F(DiagnosticsTest, DeleteStatsFutureCutoffRemovesAll) {
    init_db();

    EXPECT_TRUE(db_->InsertActivityStats({"2024-01-01T00:00:00Z", "2024-01-01T00:01:00Z", 1}));
    EXPECT_TRUE(db_->InsertDatabaseStats({"2024-01-01T00:01:00Z", 10, 4096}));

    int removed = db_->DeleteStatsBefore("2099-01-01T00:00:00Z");
    EXPECT_EQ(removed, 2);
}

TEST_F(DiagnosticsTest, InsertActivityStatsAllFields) {
    init_db();

    ActivityStatsRow row;
    row.since = "2024-06-01T00:00:00Z";
    row.until = "2024-06-01T00:01:00Z";
    row.query_count = 42;
    row.query_min = 2;
    row.query_max = 98;
    row.query_avg = 25;
    row.ingest_count = 100;
    row.ingest_size_min = 64;
    row.ingest_size_max = 8192;
    row.ingest_size_avg = 2048;
    row.ingest_drop_count = 5;
    row.insert_batch_count = 10;
    row.insert_total_count = 1000;
    row.insert_total_cost = 500;
    row.sse_session_count = 7;
    row.http_conn_count = 3;

    EXPECT_TRUE(db_->InsertActivityStats(row));
}

TEST_F(DiagnosticsTest, InsertDatabaseStatsNegativeValues) {
    init_db();

    // SQLite INTEGER columns accept negative values.
    EXPECT_TRUE(db_->InsertDatabaseStats({"2024-01-01T00:00:00Z", -1, -1}));
}

// ── End-to-end: metrics → stats row → persistence ──────────────────────────

TEST_F(DiagnosticsTest, EndToEndMetricsToPersistence) {
    init_db();

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
    EXPECT_TRUE(
        db_->InsertDatabaseStats({row.until, db_->EstimateLogRowCount(), db_->GetSizeBytes()}));

    // Verify stats are not deleted by a cutoff in ages ago.
    int removed = db_->DeleteStatsBefore("2024-01-01T00:00:00Z");
    EXPECT_EQ(removed, 0);

    // Future cutoff removes them.
    removed = db_->DeleteStatsBefore("2099-01-01T00:00:00Z");
    EXPECT_EQ(removed, 2);
}

TEST_F(DiagnosticsTest, ObservationSummaryDefaultValues) {
    tasks::detail::ObservationSummary s;
    EXPECT_EQ(s.sample_count, 0);
    EXPECT_EQ(s.item_count, 0);
    EXPECT_DOUBLE_EQ(s.value_total, 0.0);
    EXPECT_DOUBLE_EQ(s.min, 0.0);
    EXPECT_DOUBLE_EQ(s.max, 0.0);
    EXPECT_DOUBLE_EQ(s.avg, 0.0);
}

// ── Single observation covers an edge-case: min and max are each other ─────

TEST_F(DiagnosticsTest, SummarizeObservationMinMaxWithFloatExtremes) {
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

// ── No observations for a requested name yields zeroed summary ─────────────

TEST_F(DiagnosticsTest, SummarizeNoMatchingObservations) {
    std::vector<metrics::Observation> samples{
        {std::chrono::steady_clock::now(), metrics::kIngestRequest, 100.0, 1},
    };

    auto [q] = tasks::detail::summarize_observations(samples, metrics::kQueryRequest);
    EXPECT_EQ(q.sample_count, 0);
    EXPECT_DOUBLE_EQ(q.min, 0.0);
    EXPECT_DOUBLE_EQ(q.max, 0.0);
    EXPECT_DOUBLE_EQ(q.avg, 0.0);
}
