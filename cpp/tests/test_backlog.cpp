#include <gtest/gtest.h>

#include "backlog.hpp"
#include "metrics.hpp"

#include <thread>
#include <vector>

using namespace loglite;

class BacklogMetricsTest : public ::testing::Test {
   protected:
    void SetUp() override { metrics::MetricsRegistry::Instance().ResetForTest(); }
    void TearDown() override { metrics::MetricsRegistry::Instance().ResetForTest(); }
};

// ── Basic contract ────────────────────────────────────────────────────────────
TEST(BacklogTest, AddIncreasesSize) {
    Backlog backlog{10};
    backlog.Add({{"msg", "hello"}});
    EXPECT_EQ(backlog.Size(), 1u);
}

TEST(BacklogTest, FlushReturnsAllEntries) {
    Backlog backlog{10};
    backlog.Add({{"id", 1}});
    backlog.Add({{"id", 2}});
    auto entries = backlog.Flush();
    EXPECT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0]["id"].get<int>(), 1);
    EXPECT_EQ(entries[1]["id"].get<int>(), 2);
    EXPECT_EQ(backlog.Size(), 0u);
}

TEST(BacklogTest, IsFullSetAtCapacity) {
    Backlog backlog{3};
    backlog.Add({{"id", 1}});
    backlog.Add({{"id", 2}});
    EXPECT_FALSE(backlog.IsFull());
    backlog.Add({{"id", 3}});
    EXPECT_TRUE(backlog.IsFull());
}

// ── Bounded / drop-oldest behaviour ──────────────────────────────────────────

TEST(BacklogTest, BoundedDropsOldestWhenFull) {
    Backlog backlog{3};
    backlog.Add({{"id", 1}});
    backlog.Add({{"id", 2}});
    backlog.Add({{"id", 3}});  // at capacity

    // 4th entry should evict id=1 (the oldest)
    backlog.Add({{"id", 4}});

    EXPECT_EQ(backlog.Size(), 3u);
    EXPECT_TRUE(backlog.IsFull());

    auto entries = backlog.Flush();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0]["id"].get<int>(), 2);  // id=1 was dropped
    EXPECT_EQ(entries[1]["id"].get<int>(), 3);
    EXPECT_EQ(entries[2]["id"].get<int>(), 4);
}

TEST(BacklogTest, SizeNeverExceedsMaxSize) {
    constexpr size_t kMax = 5;
    Backlog backlog{kMax};
    for (int i = 0; i < 100; ++i) {
        backlog.Add({{"id", i}});
        EXPECT_LE(backlog.Size(), kMax);
    }
    EXPECT_EQ(backlog.Size(), kMax);
    EXPECT_TRUE(backlog.IsFull());
}

TEST(BacklogTest, MultipleOverflowsRetainsMostRecent) {
    // Fill to capacity then overflow by 2; the two oldest entries must be gone.
    Backlog backlog{4};
    for (int i = 1; i <= 6; ++i) backlog.Add({{"id", i}});

    auto entries = backlog.Flush();
    ASSERT_EQ(entries.size(), 4u);
    // Entries 1 and 2 were evicted; 3, 4, 5, 6 remain in order.
    for (int i = 0; i < 4; ++i) EXPECT_EQ(entries[i]["id"].get<int>(), i + 3);
}

TEST_F(BacklogMetricsTest, OverflowRecordsDropMetrics) {
    Backlog backlog{2};
    backlog.Add({{"id", 1}});
    backlog.Add({{"id", 2}});
    backlog.Add({{"id", 3}});

    auto samples = metrics::MetricsRegistry::Instance().SnapshotObservations();
    ASSERT_EQ(samples.size(), 1u);
    EXPECT_EQ(samples[0].name, metrics::kBacklogDrop);
}
