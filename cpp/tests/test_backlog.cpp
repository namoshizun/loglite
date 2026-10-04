#include <gtest/gtest.h>

#include "backlog.hpp"
#include "metrics.hpp"

#include <stdexcept>
#include <thread>
#include <vector>

using namespace loglite;

class BacklogMetricsTest : public ::testing::Test {
   protected:
    void SetUp() override { metrics::MetricsRegistry::Instance().Reset(); }
    void TearDown() override { metrics::MetricsRegistry::Instance().Reset(); }
};

// ── Basic contract ────────────────────────────────────────────────────────────

TEST(BacklogTest, ZeroCapacityThrows) { EXPECT_THROW(Backlog{0}, std::invalid_argument); }

TEST(BacklogTest, WatermarkTracksSmallAndLargeCapacitiesAndResetsAfterDrain) {
    // With max_size=3, 95% is crossed only at the hard cap; at 100 it is crossed at 95.
    for (const auto& [capacity, watermark] : {std::pair{3u, 3u}, {100u, 95u}}) {
        SCOPED_TRACE(capacity);
        Backlog backlog{capacity};
        for (unsigned id = 1; id < watermark; ++id) backlog.Add({{"id", id}});
        EXPECT_FALSE(backlog.IsFull());
        backlog.Add({{"id", watermark}});
        EXPECT_TRUE(backlog.IsFull());
        EXPECT_EQ(backlog.Size(), watermark);
        auto entries = backlog.Flush();
        ASSERT_EQ(entries.size(), watermark);
        for (unsigned i = 0; i < watermark; ++i) EXPECT_EQ(entries[i]["id"], i + 1);
        EXPECT_EQ(backlog.Size(), 0u);
        EXPECT_FALSE(backlog.IsFull());
    }
}

// ── Bounded / drop-oldest behaviour ──────────────────────────────────────────

TEST_F(BacklogMetricsTest, OverflowPreservesNewestEntriesInOrderAndCountsEveryDrop) {
    for (const auto& [capacity, inputs] : {std::pair{3, 4}, {4, 6}, {5, 100}}) {
        SCOPED_TRACE(capacity);
        Backlog backlog{static_cast<size_t>(capacity)};
        for (int id = 1; id <= inputs; ++id) {
            backlog.Add({{"id", id}});
            EXPECT_LE(backlog.Size(), capacity);
        }
        EXPECT_TRUE(backlog.IsFull());
        const auto entries = backlog.Flush();
        ASSERT_EQ(entries.size(), capacity);
        // Entries before inputs-capacity+1 were evicted; the rest remain in order.
        for (int i = 0; i < capacity; ++i) EXPECT_EQ(entries[i]["id"], inputs - capacity + i + 1);
        const auto samples = metrics::MetricsRegistry::Instance().Flush();
        int64_t dropped = 0;
        for (const auto& sample : samples) {
            EXPECT_EQ(sample.name, metrics::kBacklogDrop);
            dropped += sample.item_count;
        }
        EXPECT_EQ(dropped, inputs - capacity);
    }
}

TEST(BacklogTest, FailedPersistenceRestoresBatchBeforeNewEntries) {
    Backlog backlog{10};
    backlog.Add({{"id", 1}});
    backlog.Add({{"id", 2}});
    EXPECT_THROW(backlog.Flush([&](const auto&) -> int {
        std::thread producer{[&] { backlog.Add({{"id", 3}}); }};
        producer.join();  // persistence must not hold the backlog mutex
        throw std::runtime_error("write failed");
    }),
                 std::runtime_error);
    EXPECT_EQ(backlog.Size(), 3u);
    EXPECT_EQ(backlog.Flush([](const auto& entries) {
        EXPECT_EQ(entries, (std::vector<nlohmann::json>{{{"id", 1}}, {{"id", 2}}, {{"id", 3}}}));
        return static_cast<int>(entries.size());
    }),
              3);
    EXPECT_EQ(backlog.Size(), 0u);
    EXPECT_EQ(backlog.Flush([](const auto&) -> int {
        ADD_FAILURE() << "An empty backlog should not call persistence";
        return 0;
    }),
              0);
}

TEST_F(BacklogMetricsTest, FailedPersistencePreservesCapacityAndCountsOverflow) {
    Backlog backlog{3};
    for (int id = 1; id <= 3; ++id) backlog.Add({{"id", id}});
    EXPECT_THROW(backlog.Flush([&](const auto&) -> int {
        backlog.Add({{"id", 4}});
        backlog.Add({{"id", 5}});
        throw std::runtime_error("write failed");
    }),
                 std::runtime_error);

    EXPECT_TRUE(backlog.IsFull());
    auto entries = backlog.Flush();
    ASSERT_EQ(entries.size(), 3u);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(entries[i]["id"], i + 3);
    auto samples = metrics::MetricsRegistry::Instance().Flush();
    ASSERT_EQ(samples.size(), 1u);
    EXPECT_EQ(samples[0].name, metrics::kBacklogDrop);
    EXPECT_EQ(samples[0].item_count, 2);
}
