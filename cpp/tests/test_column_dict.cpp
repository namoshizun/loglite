#include <gtest/gtest.h>

#include "column_dict.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace loglite;
using namespace std::chrono_literals;

// ── Fixture ────────────────────────────────────────────────────────────────────

class ColumnDictFixture : public ::testing::Test {
   protected:
    void SetUp() override {
        persisted_.clear();
        calls_.store(0);
    }

    void TearDown() override {}

    ColumnDictionary::PersistFn make_persist() {
        return [this](const std::string& c, const std::string& v, int id) {
            std::lock_guard lk{persist_mtx_};
            persisted_.emplace_back(c, v, id);
            ++calls_;
        };
    }

    std::vector<std::tuple<std::string, std::string, int>> persisted_;
    std::mutex persist_mtx_;
    std::atomic<int> calls_{0};
};

// ── Basic unit tests ───────────────────────────────────────────────────────────

TEST_F(ColumnDictFixture, GetOrCreateReturnsSequentialIds) {
    LookupTable lut;
    ColumnDictionary dict{lut, make_persist()};

    auto id1 = dict.GetOrCreate("level", "DEBUG");
    auto id2 = dict.GetOrCreate("level", "INFO");
    auto id3 = dict.GetOrCreate("level", "WARNING");
    auto id4 = dict.GetOrCreate("level", "ERROR");

    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);
    EXPECT_EQ(id3, 3);
    EXPECT_EQ(id4, 4);
    EXPECT_EQ(calls_.load(), 4);

    // Re-fetching returns same id.
    EXPECT_EQ(dict.GetOrCreate("level", "INFO"), 2);
    EXPECT_EQ(calls_.load(), 4);  // no new persist call
}

TEST_F(ColumnDictFixture, GetOrCreateAcrossMultipleColumns) {
    LookupTable lut;
    ColumnDictionary dict{lut, make_persist()};

    // Each column has its own id namespace starting from 1.
    EXPECT_EQ(dict.GetOrCreate("service", "auth"), 1);
    EXPECT_EQ(dict.GetOrCreate("service", "gateway"), 2);
    EXPECT_EQ(dict.GetOrCreate("level", "INFO"), 1);
    EXPECT_EQ(dict.GetOrCreate("level", "ERROR"), 2);

    EXPECT_EQ(calls_.load(), 4);
}

TEST_F(ColumnDictFixture, GetOrCreateWithNullPersist) {
    LookupTable lut;
    ColumnDictionary dict{lut, nullptr};

    auto id = dict.GetOrCreate("col", "val");
    EXPECT_EQ(id, 1);
    // nullptr persist → no crash
}

TEST_F(ColumnDictFixture, GetValueReturnsCorrectStrings) {
    LookupTable lut{{"level", {{"INFO", 1}, {"ERROR", 2}, {"DEBUG", 3}}}};
    ColumnDictionary dict{lut, nullptr};

    EXPECT_EQ(dict.GetValue("level", 1), "INFO");
    EXPECT_EQ(dict.GetValue("level", 2), "ERROR");
    EXPECT_EQ(dict.GetValue("level", 3), "DEBUG");
}

TEST_F(ColumnDictFixture, GetValueThrowsOnUnknownColumn) {
    LookupTable lut;
    ColumnDictionary dict{lut, nullptr};
    EXPECT_THROW(dict.GetValue("nonexistent", 1), std::runtime_error);
}

TEST_F(ColumnDictFixture, GetValueThrowsOnUnknownId) {
    LookupTable lut{{"level", {{"INFO", 1}}}};
    ColumnDictionary dict{lut, nullptr};
    EXPECT_THROW(dict.GetValue("level", 999), std::runtime_error);
}

TEST_F(ColumnDictFixture, QueryCandidatesAllOperators) {
    LookupTable lut{{"level", {{"debug", 1}, {"info", 2}, {"warning", 3}, {"error", 4}}}};
    ColumnDictionary dict{lut, nullptr};

    // =
    EXPECT_EQ(dict.QueryCandidates({"level", "=", "info"}).size(), 1u);
    // !=
    EXPECT_EQ(dict.QueryCandidates({"level", "!=", "info"}).size(), 3u);
    // >
    EXPECT_EQ(dict.QueryCandidates({"level", ">", "info"}).size(), 1u);  // warning
    // >=
    EXPECT_EQ(dict.QueryCandidates({"level", ">=", "info"}).size(), 2u);  // info, warning
    // <
    EXPECT_EQ(dict.QueryCandidates({"level", "<", "info"}).size(), 2u);  // debug, error
    // <=
    EXPECT_EQ(dict.QueryCandidates({"level", "<=", "info"}).size(), 3u);  // debug, error, info
    // ~= (substring)
    EXPECT_EQ(dict.QueryCandidates({"level", "~=", "warn"}).size(), 1u);
    EXPECT_EQ(dict.QueryCandidates({"level", "~=", "XXX"}).size(), 0u);
}

TEST_F(ColumnDictFixture, QueryCandidatesUnknownColumnReturnsEmpty) {
    LookupTable lut;
    ColumnDictionary dict{lut, nullptr};
    auto ids = dict.QueryCandidates({"nonexistent", "=", "val"});
    EXPECT_TRUE(ids.empty());
}

TEST_F(ColumnDictFixture, QueryCandidatesNonStringValue) {
    LookupTable lut{{"count", {{"1", 1}, {"2", 2}, {"3", 3}}}};
    ColumnDictionary dict{lut, nullptr};

    // Filter value is an integer — QueryCandidates stringifies it via json::dump().
    QueryFilter f{"count", "=", 2};
    auto ids = dict.QueryCandidates(f);
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], 2);
}

TEST_F(ColumnDictFixture, GetLookUpReturnsSnapshot) {
    LookupTable lut{{"level", {{"INFO", 1}, {"ERROR", 2}}}};
    ColumnDictionary dict{lut, make_persist()};

    // Add a new entry via GetOrCreate.
    dict.GetOrCreate("level", "WARNING");

    auto snap = dict.GetLookUp();
    EXPECT_EQ(snap.at("level").size(), 3u);
    EXPECT_EQ(snap.at("level").at("INFO"), 1);
    EXPECT_EQ(snap.at("level").at("ERROR"), 2);
    EXPECT_EQ(snap.at("level").at("WARNING"), 3);

    // Mutating the snapshot does not affect the dictionary.
    snap["level"]["NEW"] = 99;
    EXPECT_EQ(dict.GetLookUp().at("level").size(), 3u);
}

TEST_F(ColumnDictFixture, GetLookUpEmptyDict) {
    LookupTable lut;
    ColumnDictionary dict{lut, nullptr};
    auto snap = dict.GetLookUp();
    EXPECT_TRUE(snap.empty());
}

// ── Concurrent read/write tests ────────────────────────────────────────────────

// Stress test: one writer continuously calls GetOrCreate while multiple readers
// call GetValue, QueryCandidates, and GetLookUp concurrently.
// The test verifies:
//   1. No crashes (segfault, double-free, iterator invalidation).
//   2. All written entries are eventually visible and stable.

TEST_F(ColumnDictFixture, ConcurrentReadsAndWritesNoCrash) {
    LookupTable lut;
    ColumnDictionary dict{lut, make_persist()};

    std::atomic<bool> done{false};
    std::atomic<int> write_count{0};

    constexpr int kReaderThreads = 4;
    constexpr auto kDuration = 500ms;

    // ── Writer thread ──
    std::thread writer{[&]() {
        int i = 0;
        while (!done.load(std::memory_order_relaxed)) {
            dict.GetOrCreate("col_a", "val_" + std::to_string(i));
            dict.GetOrCreate("col_b", "val_" + std::to_string(i));
            ++write_count;
            ++i;
        }
    }};

    // ── Reader threads ──
    std::vector<std::thread> readers;
    for (int t = 0; t < kReaderThreads; ++t) {
        readers.emplace_back([&]() {
            while (!done.load(std::memory_order_relaxed)) {
                // GetValue on a known id (may or may not exist yet — we catch).
                try {
                    (void)dict.GetValue("col_a", 1);
                } catch (const std::runtime_error&) {
                    // Expected for ids that haven't been created yet.
                }

                // QueryCandidates on both columns.
                (void)dict.QueryCandidates({"col_a", "=", "val_0"});
                (void)dict.QueryCandidates({"col_b", "~=", "val"});

                // GetLookUp snapshot.
                (void)dict.GetLookUp();
            }
        });
    }

    // Let them run for the chosen duration.
    std::this_thread::sleep_for(kDuration);
    done.store(true, std::memory_order_relaxed);

    writer.join();
    for (auto& r : readers) r.join();

    // Final state consistency: all writes should be visible.
    auto snap = dict.GetLookUp();
    EXPECT_TRUE(snap.contains("col_a"));
    EXPECT_TRUE(snap.contains("col_b"));

    // Every written id should resolve to a non-empty string.
    for (const auto& [col_name, col_map] : snap) {
        for (const auto& [val, id] : col_map) {
            std::string resolved = dict.GetValue(col_name, id);
            EXPECT_EQ(resolved, val) << "id=" << id << " expected=" << val << " got=" << resolved;
        }
    }

    // Ids within each column should be monotonic starting from 1 with no gaps
    // (since writes are serialised by the strand equivalent in this test —
    //  GetOrCreate uses unique_lock internally).
    for (const auto& [col_name, col_map] : snap) {
        std::set<int> ids;
        for (const auto& [_, id] : col_map) ids.insert(id);
        int expected = 1;
        for (int id : ids) {
            EXPECT_EQ(id, expected) << "gap or duplicate in column " << col_name;
            ++expected;
        }
    }

    // Writes were interleaved equally between col_a and col_b; each column
    // should have approximately the same number of entries.
    ASSERT_GT(snap.at("col_a").size(), 0u);
    ASSERT_GT(snap.at("col_b").size(), 0u);
    auto delta = std::max(snap.at("col_a").size(), snap.at("col_b").size()) -
                 std::min(snap.at("col_a").size(), snap.at("col_b").size());
    EXPECT_LE(delta, 1u) << "columns should have roughly equal entries";

    // Persist callback must have been called for each unique (col, value) pair.
    EXPECT_EQ(calls_.load(), static_cast<int>(snap.at("col_a").size() + snap.at("col_b").size()));
}

// Simulate many writers calling GetOrCreate on the same column simultaneously.
// Verifies no duplicate ids are assigned and no crash occurs.
TEST_F(ColumnDictFixture, ConcurrentWritersOnSameColumn) {
    LookupTable lut;
    ColumnDictionary dict{lut, make_persist()};

    constexpr int kWriters = 8;
    constexpr int kWritesPerThread = 500;

    std::vector<std::thread> writers;
    for (int t = 0; t < kWriters; ++t) {
        writers.emplace_back([&dict, t]() {
            for (int i = 0; i < kWritesPerThread; ++i) {
                std::string val = "val_" + std::to_string(t) + "_" + std::to_string(i);
                dict.GetOrCreate("shared_col", val);
            }
        });
    }

    for (auto& w : writers) w.join();

    auto snap = dict.GetLookUp();
    ASSERT_TRUE(snap.contains("shared_col"));
    const auto& col_map = snap.at("shared_col");

    // Total entries = kWriters * kWritesPerThread (all values are unique).
    EXPECT_EQ(col_map.size(), kWriters * kWritesPerThread);

    // All ids from 1 to N must be present exactly once.
    std::set<int> ids;
    for (const auto& [_, id] : col_map) ids.insert(id);
    EXPECT_EQ(ids.size(), col_map.size());  // no duplicates
    EXPECT_EQ(*ids.begin(), 1);
    EXPECT_EQ(*ids.rbegin(), static_cast<int>(col_map.size()));  // no gaps

    // Reverse-lookup consistency.
    for (const auto& [val, id] : col_map) {
        EXPECT_EQ(dict.GetValue("shared_col", id), val);
    }
}

// Readers on one column while a writer mutates another column.
// This exercises inter-column isolation — mutations to col_a must not
// invalidate iterators into col_b.
TEST_F(ColumnDictFixture, ReadOneColumnWriteAnother) {
    // Pre-populate col_b with a large set so readers have real work to do.
    LookupTable lut;
    for (int i = 0; i < 200; ++i) {
        lut["col_b"]["val_" + std::to_string(i)] = i + 1;
    }
    ColumnDictionary dict{lut, make_persist()};

    std::atomic<bool> done{false};
    std::atomic<int> reads{0};

    // Writer: continuously insert into col_a.
    std::thread writer{[&]() {
        int i = 0;
        while (!done.load(std::memory_order_relaxed)) {
            dict.GetOrCreate("col_a", "val_" + std::to_string(i));
            ++i;
        }
    }};

    // Readers: query col_b repeatedly (should be stable).
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&]() {
            while (!done.load(std::memory_order_relaxed)) {
                auto ids = dict.QueryCandidates({"col_b", "~=", "val_1"});
                EXPECT_FALSE(ids.empty()) << "col_b should be stable; got empty result";
                // GetValue on a stable id.
                auto val = dict.GetValue("col_b", 1);
                EXPECT_EQ(val, "val_0");
                ++reads;
            }
        });
    }

    std::this_thread::sleep_for(300ms);
    done.store(true, std::memory_order_relaxed);

    writer.join();
    for (auto& r : readers) r.join();

    // col_b must have exactly 200 entries — no corruption.
    auto snap = dict.GetLookUp();
    ASSERT_TRUE(snap.contains("col_b"));
    EXPECT_EQ(snap.at("col_b").size(), 200u);

    // col_a must have been populated.
    ASSERT_TRUE(snap.contains("col_a"));
    EXPECT_GT(snap.at("col_a").size(), 0u);

    // Every read should have succeeded.
    EXPECT_GT(reads.load(), 0);
}