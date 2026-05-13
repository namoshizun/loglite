#include <gtest/gtest.h>

#include "metrics.hpp"

#include <chrono>
#include <thread>

using namespace loglite;

class MetricsTest : public ::testing::Test {
   protected:
    void SetUp() override { metrics::MetricsRegistry::Instance().Reset(); }

    void TearDown() override { metrics::MetricsRegistry::Instance().Reset(); }
};

TEST_F(MetricsTest, CollectStoresNamedObservations) {
    auto& registry = metrics::MetricsRegistry::Instance();

    registry.Collect(metrics::kQueryRequest, 12.5);
    registry.Collect(metrics::kInsertBatch, 8.0, 3);

    auto samples = registry.Flush();
    ASSERT_EQ(samples.size(), 2u);
    EXPECT_EQ(samples[0].name, metrics::kQueryRequest);
    EXPECT_DOUBLE_EQ(samples[0].value, 12.5);
    EXPECT_EQ(samples[0].item_count, 1);
    EXPECT_EQ(samples[1].name, metrics::kInsertBatch);
    EXPECT_DOUBLE_EQ(samples[1].value, 8.0);
    EXPECT_EQ(samples[1].item_count, 3);
}

TEST_F(MetricsTest, SnapshotPrunesExpiredObservations) {
    auto& registry = metrics::MetricsRegistry::Instance();
    registry.Configure(std::chrono::milliseconds{20});

    registry.Collect(metrics::kQueryRequest, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    registry.Collect(metrics::kIngestRequest, 2.0);

    auto samples = registry.Flush();
    ASSERT_EQ(samples.size(), 1u);
    EXPECT_EQ(samples[0].name, metrics::kIngestRequest);
}

TEST_F(MetricsTest, GaugesTrackLiveCounts) {
    auto& registry = metrics::MetricsRegistry::Instance();

    registry.IncrementGauge(metrics::kHttpConnection);
    registry.IncrementGauge(metrics::kHttpConnection);
    registry.DecrementGauge(metrics::kHttpConnection);

    EXPECT_EQ(registry.Gauge(metrics::kHttpConnection), 1);
}

TEST_F(MetricsTest, GaugeGuardBalancesGauge) {
    auto& registry = metrics::MetricsRegistry::Instance();

    {
        metrics::GaugeGuard guard{metrics::kSseSession};
        EXPECT_EQ(registry.Gauge(metrics::kSseSession), 1);
    }

    EXPECT_EQ(registry.Gauge(metrics::kSseSession), 0);
}

TEST_F(MetricsTest, ObservationTimerCollectsElapsedTime) {
    auto& registry = metrics::MetricsRegistry::Instance();

    {
        metrics::ObservationTimer timer{metrics::kQueryRequest};
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    auto samples = registry.Flush();
    ASSERT_EQ(samples.size(), 1u);
    EXPECT_EQ(samples[0].name, metrics::kQueryRequest);
    EXPECT_GT(samples[0].value, 0.0);
}
