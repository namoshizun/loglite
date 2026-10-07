#include <gtest/gtest.h>

#include "backlog.hpp"
#include "metrics.hpp"

#include <stdexcept>
#include <thread>
#include <vector>

using namespace loglite;

namespace {

PreparedEntry Entry(int id) {
    PreparedEntry entry;
    entry.fields = {{"id", id}};
    entry.bytes = 1;
    return entry;
}

}  // namespace

class BacklogMetricsTest : public ::testing::Test {
   protected:
    void SetUp() override { metrics::MetricsRegistry::Instance().Reset(); }
    void TearDown() override { metrics::MetricsRegistry::Instance().Reset(); }
};

TEST(BacklogTest, ZeroCapacityThrows) { EXPECT_THROW(Backlog{0}, std::invalid_argument); }

TEST(BacklogTest, WatermarkTracksSmallAndLargeCapacitiesAndResetsAfterDrain) {
    for (const auto& [capacity, watermark] : {std::pair{3u, 3u}, {100u, 95u}}) {
        SCOPED_TRACE(capacity);
        Backlog backlog{capacity};
        for (unsigned id = 1; id < watermark; ++id) backlog.Add(Entry(static_cast<int>(id)));
        EXPECT_FALSE(backlog.IsFull());
        backlog.Add(Entry(static_cast<int>(watermark)));
        EXPECT_TRUE(backlog.IsFull());
        EXPECT_EQ(backlog.Size(), watermark);
        auto entries = backlog.Take();
        backlog.NoteSettled(entries.size(), entries.size());
        ASSERT_EQ(entries.size(), watermark);
        for (unsigned i = 0; i < watermark; ++i) EXPECT_EQ(entries[i].fields["id"], i + 1);
        EXPECT_EQ(backlog.Size(), 0u);
        EXPECT_FALSE(backlog.IsFull());
    }
}

TEST_F(BacklogMetricsTest, OverflowPreservesNewestEntriesInOrderAndCountsEveryDrop) {
    for (const auto& [capacity, inputs] : {std::pair{3, 4}, {4, 6}, {5, 100}}) {
        SCOPED_TRACE(capacity);
        Backlog backlog{static_cast<size_t>(capacity)};
        for (int id = 1; id <= inputs; ++id) {
            backlog.Add(Entry(id));
            EXPECT_LE(backlog.Size(), capacity);
        }
        EXPECT_TRUE(backlog.IsFull());
        const auto entries = backlog.Take();
        backlog.NoteSettled(entries.size(), entries.size());
        ASSERT_EQ(entries.size(), capacity);
        for (int i = 0; i < capacity; ++i)
            EXPECT_EQ(entries[i].fields["id"], inputs - capacity + i + 1);
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
    backlog.Add(Entry(1));
    backlog.Add(Entry(2));
    auto taken = backlog.Take();
    std::thread producer{[&] { backlog.Add(Entry(3)); }};
    producer.join();
    backlog.NoteSettled(taken.size(), taken.size());
    backlog.Restore(std::move(taken));
    EXPECT_EQ(backlog.Size(), 3u);
    EXPECT_EQ(backlog.QueuedFields(),
              (std::vector<nlohmann::json>{{{"id", 1}}, {{"id", 2}}, {{"id", 3}}}));
}

TEST_F(BacklogMetricsTest, InFlightEntriesCountTowardCapacity) {
    Backlog backlog{3};
    for (int id = 1; id <= 3; ++id) backlog.Add(Entry(id));
    auto taken = backlog.Take();
    backlog.Add(Entry(4));
    backlog.Add(Entry(5));
    EXPECT_EQ(backlog.Size(), 0u);
    backlog.NoteSettled(taken.size(), taken.size());
    backlog.Restore(std::move(taken));
    EXPECT_EQ(backlog.QueuedFields(),
              (std::vector<nlohmann::json>{{{"id", 1}}, {{"id", 2}}, {{"id", 3}}}));
    auto samples = metrics::MetricsRegistry::Instance().Flush();
    int64_t dropped = 0;
    for (const auto& sample : samples) dropped += sample.item_count;
    EXPECT_EQ(dropped, 2);
}
