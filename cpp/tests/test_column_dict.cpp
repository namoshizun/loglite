#include <gtest/gtest.h>

#include "column_dict.hpp"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace loglite;

// ── Fixture ───────────────────────────────────────────────────────────────────

class ColumnDictFixture : public ::testing::Test {
   protected:
    ColumnDictionary::PersistFn MakePersist() {
        return [this](const std::string& column, const std::string& value, int id) {
            std::lock_guard lock{persist_mutex_};
            persisted_.emplace_back(column, value, id);
            ++calls_;
            return true;
        };
    }

    void ExpectColumn(const ColumnDictionary& dictionary, const std::string& name, size_t size) {
        const auto snapshot = dictionary.GetLookUp();
        ASSERT_TRUE(snapshot.contains(name));
        const auto& column = snapshot.at(name);
        EXPECT_EQ(column.size(), size);
        std::set<int> ids;
        for (const auto& [value, id] : column) {
            EXPECT_EQ(dictionary.GetValue(name, id), value);
            ids.insert(id);
        }
        // IDs start at 1, with no duplicates or gaps; every ID must reverse-lookup correctly.
        ASSERT_EQ(ids.size(), size);
        ASSERT_FALSE(ids.empty());
        EXPECT_EQ(*ids.begin(), 1);
        EXPECT_EQ(*ids.rbegin(), size);
    }

    std::vector<std::tuple<std::string, std::string, int>> persisted_;
    std::mutex persist_mutex_;
    std::atomic<int> calls_{0};
};

// ── Mapping, persistence and snapshot ownership ────────────────────────────────

TEST_F(ColumnDictFixture, IDsAreStablePerColumnAndSnapshotsAreIndependent) {
    ColumnDictionary dictionary{{{"level", {{"DEBUG", 1}, {"INFO", 2}}}}, MakePersist()};
    EXPECT_EQ(dictionary.GetValue("level", 1), "DEBUG");
    EXPECT_EQ(dictionary.GetValue("level", 2), "INFO");
    // Re-fetching returns the same ID without another persist call.
    EXPECT_EQ(dictionary.GetOrCreate("level", "INFO"), 2);
    EXPECT_EQ(calls_.load(), 0);
    EXPECT_EQ(dictionary.GetOrCreate("level", "WARNING"), 3);
    EXPECT_EQ(dictionary.GetOrCreate("level", "ERROR"), 4);
    // Each column has its own ID namespace starting from 1.
    EXPECT_EQ(dictionary.GetOrCreate("service", "auth"), 1);
    EXPECT_EQ(dictionary.GetOrCreate("service", "gateway"), 2);
    EXPECT_EQ(persisted_, (decltype(persisted_){{"level", "WARNING", 3},
                                                {"level", "ERROR", 4},
                                                {"service", "auth", 1},
                                                {"service", "gateway", 2}}));
    auto snapshot = dictionary.GetLookUp();
    snapshot["level"]["NEW"] = 99;
    EXPECT_EQ(dictionary.GetOrCreate("level", "TRACE"), 5);
    EXPECT_FALSE(snapshot.at("level").contains("TRACE"));
    EXPECT_FALSE(dictionary.GetLookUp().at("level").contains("NEW"));
    EXPECT_EQ(calls_.load(), 5);
    ExpectColumn(dictionary, "level", 5);
    ExpectColumn(dictionary, "service", 2);
}

TEST_F(ColumnDictFixture, OptionalPersistenceAndUnknownLookupsHaveExplicitOutcomes) {
    ColumnDictionary dictionary{{}, nullptr};
    EXPECT_EQ(dictionary.GetOrCreate("level", "INFO"), 1);
    EXPECT_EQ(dictionary.GetValue("level", 1), "INFO");
    EXPECT_THROW(dictionary.GetValue("nonexistent", 1), std::runtime_error);
    EXPECT_THROW(dictionary.GetValue("level", 999), std::runtime_error);
    EXPECT_TRUE(dictionary.QueryCandidates({"nonexistent", "=", "val"}).empty());
}

TEST_F(ColumnDictFixture, CandidateOperatorsSelectExactIDs) {
    ColumnDictionary dictionary{
        {{"level", {{"debug", 1}, {"info", 2}, {"warning", 3}, {"error", 4}}}}, nullptr};
    struct Case {
        const char* op;
        const char* value;
        std::vector<ValueId> ids;
    };
    const Case cases[]{
        {"=", "info", {2}},
        {"!=", "info", {1, 3, 4}},
        {">", "info", {3}},
        {">=", "info", {2, 3}},
        {"<", "info", {1, 4}},
        {"<=", "info", {1, 2, 4}},
        {"~=", "warn", {3}},
        {"~=", "XXX", {}},
        {"~=", "err", {4}},
        // ~= is one-directional: "erroring" must not match stored "error".
        {"~=", "erroring", {}},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(std::string{c.op} + c.value);
        auto ids = dictionary.QueryCandidates({"level", c.op, c.value});
        std::ranges::sort(ids);
        EXPECT_EQ(ids, c.ids);
    }
    ColumnDictionary counts{{{"count", {{"1", 1}, {"2", 2}, {"3", 3}}}}, nullptr};
    // Filter value is an integer — QueryCandidates stringifies it via json::dump().
    EXPECT_EQ(counts.QueryCandidates({"count", "=", 2}), (std::vector<ValueId>{2}));
}

// ── Concurrent read/write tests ────────────────────────────────────────────────

// One writer grows both columns while readers use forward, reverse and snapshot lookups.
// Bounded rounds ensure every reader participates and keep work independent of CPU speed.
TEST_F(ColumnDictFixture, ConcurrentReadersObserveStableValuesWhileColumnsGrow) {
    ColumnDictionary dictionary{{}, MakePersist()};
    dictionary.GetOrCreate("col_a", "val_0");
    dictionary.GetOrCreate("col_b", "val_0");
    constexpr int kRounds = 200;
    constexpr int kReaders = 4;
    std::barrier phase{kReaders + 1};
    std::jthread writer{[&] {
        for (int i = 1; i <= kRounds; ++i) {
            phase.arrive_and_wait();
            dictionary.GetOrCreate("col_a", "val_" + std::to_string(i));
            dictionary.GetOrCreate("col_b", "val_" + std::to_string(i));
        }
    }};
    std::vector<std::jthread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            for (int round = 0; round < kRounds; ++round) {
                phase.arrive_and_wait();
                EXPECT_EQ(dictionary.GetValue("col_a", 1), "val_0");
                EXPECT_EQ(dictionary.QueryCandidates({"col_a", "=", "val_0"}),
                          (std::vector<ValueId>{1}));
                EXPECT_FALSE(dictionary.QueryCandidates({"col_b", "~=", "val"}).empty());
                const auto snapshot = dictionary.GetLookUp();
                EXPECT_EQ(snapshot.at("col_a").at("val_0"), 1);
                EXPECT_EQ(snapshot.at("col_b").at("val_0"), 1);
            }
        });
    }
    writer.join();
    for (auto& reader : readers) reader.join();
    ExpectColumn(dictionary, "col_a", kRounds + 1);
    ExpectColumn(dictionary, "col_b", kRounds + 1);
    EXPECT_EQ(calls_.load(), 2 * (kRounds + 1));
}

// Many writers contend for both unique and shared values in the same column.
TEST_F(ColumnDictFixture, ConcurrentWritersAssignUniqueIDsAndPersistSharedValuesOnce) {
    ColumnDictionary dictionary{{}, MakePersist()};
    constexpr int kWriters = 8;
    constexpr int kWritesPerThread = 100;
    std::barrier start{kWriters};
    std::vector<std::jthread> writers;
    for (int thread = 0; thread < kWriters; ++thread) {
        writers.emplace_back([&, thread] {
            start.arrive_and_wait();
            for (int i = 0; i < kWritesPerThread; ++i) {
                dictionary.GetOrCreate("shared_col",
                                       "val_" + std::to_string(thread) + "_" + std::to_string(i));
                dictionary.GetOrCreate("shared_col", "shared_" + std::to_string(i));
            }
        });
    }
    for (auto& writer : writers) writer.join();
    constexpr int kUniqueValues = (kWriters + 1) * kWritesPerThread;
    ExpectColumn(dictionary, "shared_col", kUniqueValues);
    EXPECT_EQ(calls_.load(), kUniqueValues);
}

// Readers on col_b must stay stable while a writer creates/grows col_a.
TEST_F(ColumnDictFixture, ReadOneColumnWriteAnother) {
    LookupTable lookup;
    for (int i = 0; i < 200; ++i) lookup["col_b"]["val_" + std::to_string(i)] = i + 1;
    ColumnDictionary dictionary{lookup, MakePersist()};
    constexpr int kRounds = 100;
    constexpr int kReaders = 4;
    std::barrier phase{kReaders + 1};
    std::jthread writer{[&] {
        for (int i = 0; i < kRounds; ++i) {
            phase.arrive_and_wait();
            dictionary.GetOrCreate("col_a", "val_" + std::to_string(i));
        }
    }};
    std::vector<std::jthread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            for (int round = 0; round < kRounds; ++round) {
                phase.arrive_and_wait();
                EXPECT_EQ(dictionary.QueryCandidates({"col_b", "~=", "val_1"}).size(), 111u);
                EXPECT_EQ(dictionary.GetValue("col_b", 1), "val_0");
            }
        });
    }
    writer.join();
    for (auto& reader : readers) reader.join();
    ExpectColumn(dictionary, "col_a", kRounds);
    ExpectColumn(dictionary, "col_b", 200);
    EXPECT_EQ(calls_.load(), kRounds);
}
