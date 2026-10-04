#include <gtest/gtest.h>

#include "log_reader.hpp"
#include "log_store.hpp"
#include "notifier.hpp"
#include "log_store_test_support.hpp"
#include "utils.hpp"

#include <array>
#include <exception>
#include <latch>
#include <memory>
#include <thread>

using namespace loglite;

namespace {

nlohmann::json Log(std::string timestamp, std::string value) {
    return {{"timestamp", std::move(timestamp)}, {"message", value}, {"level", std::move(value)}};
}

TEST(LogReaderTest, SingleFileIdPagesUseGlobalIdOrderAndSQLiteLimits) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    LogStore store{config};
    store.Open();
    store.Initialize();
    ASSERT_EQ(
        store.Insert({Log("2024-01-03T00:00:00Z", "first"), Log("2024-01-01T00:00:00Z", "second"),
                      Log("2024-01-02T00:00:00Z", "third")}),
        3);
    LogReaderPool readers{store, 1};
    auto first = readers.UseConnection(
        [](LogReader& reader) { return reader.QueryLogIdRange({"message"}, 0, 3, 2); });
    EXPECT_EQ(first.last_id, 2);
    EXPECT_EQ(first.results,
              (std::vector<nlohmann::json>{{{"message", "first"}}, {{"message", "second"}}}));
    const auto remainder = readers.UseConnection([&](LogReader& reader) {
        return reader.QueryLogIdRange({"message"}, first.last_id, 3, -1);
    });
    EXPECT_EQ(remainder.last_id, 3);
    EXPECT_EQ(remainder.results, (std::vector<nlohmann::json>{{{"message", "third"}}}));
    const auto empty = readers.UseConnection(
        [](LogReader& reader) { return reader.QueryLogIdRange({"id"}, 1, 3, 0); });
    EXPECT_EQ(empty.last_id, 1);
    EXPECT_TRUE(empty.results.empty());
}

TEST(LogReaderTest, SingleFileWatermarkTracksRowIdsReusedAfterDeletion) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    LogStore store{config};
    store.Open();
    store.Initialize();
    ASSERT_EQ(
        store.Insert({Log("2024-01-01T00:00:00Z", "first"), Log("2024-01-02T00:00:00Z", "second"),
                      Log("2024-01-03T00:00:00Z", "third")}),
        3);
    LogNotifier notifier;
    notifier.Notify(store.GetCommittedLogId());
    const nlohmann::json invalid{{"timestamp", "2024-01-04T00:00:00Z"}};
    test::ExecuteFile(config.db_path, "DELETE FROM TestLog WHERE id >= 2");
    ASSERT_EQ(store.Insert({Log("2024-01-04T00:00:00Z", "reused two"), invalid}), 1);
    EXPECT_EQ(store.GetCommittedLogId(), 2);
    notifier.Notify(store.GetCommittedLogId());
    // New SSE subscribers take their starting cursor from this value.
    EXPECT_EQ(notifier.GetLastId(), 2);
    test::ExecuteFile(config.db_path, "DELETE FROM TestLog");
    ASSERT_EQ(store.Insert({invalid}), 0);
    EXPECT_EQ(store.GetCommittedLogId(), 2);  // nothing new was committed
    ASSERT_EQ(store.Insert({invalid, Log("2024-01-05T00:00:00Z", "reused one")}), 1);
    EXPECT_EQ(store.GetCommittedLogId(), 1);
    notifier.Notify(store.GetCommittedLogId());
    EXPECT_EQ(notifier.GetLastId(), 1);
}

TEST(LogReaderTest, AsyncViewsKeepMoveOnlyCallbacksAndReleaseConnectionsAfterFailure) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    LogStore store{config};
    store.Open();
    store.Initialize();
    LogReaderPool readers{store, 1};
    asio::thread_pool executor{1};
    auto value = asio::co_spawn(
        executor,
        readers.AsyncUseConnection(executor.get_executor(),
                                   [value = std::make_unique<int>(42)](LogReader& reader) {
                                       if (!reader.Ping())
                                           throw std::runtime_error("reader unavailable");
                                       return *value;
                                   }),
        asio::use_future);
    EXPECT_EQ(value.get(), 42);
    auto failure =
        asio::co_spawn(executor,
                       readers.AsyncUseConnection(
                           executor.get_executor(),
                           [](LogReader&) -> int { throw std::runtime_error("callback failed"); }),
                       asio::use_future);
    EXPECT_THROW((void)failure.get(), std::runtime_error);
    EXPECT_TRUE(readers.UseConnection([](LogReader& reader) { return reader.Ping(); }));
    executor.join();
}

TEST(LogReaderTest, ConcurrentReadsFollowFileAndDictionaryLifetimes) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    config.partition_interval = PartitionInterval::kDaily;
    config.vacuum_max_days = 1;
    config.compression = {true, {"level"}};
    config.sqlite_params["journal_mode"] = "WAL";
    config.migrations = {{1,
                          {"CREATE TABLE TestLog (id INTEGER PRIMARY KEY, timestamp TEXT NOT NULL, "
                           "message TEXT NOT NULL, level INTEGER NOT NULL)"},
                          {"DROP TABLE TestLog"}}};
    LogStore store{config};
    store.Open();
    store.Initialize();
    ASSERT_EQ(store.Insert({Log("2024-01-01T00:00:00Z", "A"), Log("2024-01-02T00:00:00Z", "B")}),
              2);
    LogReaderPool readers{store, 2};
    std::latch start{1};
    std::array<std::exception_ptr, 2> failures;
    std::vector<std::jthread> queries;
    for (size_t worker = 0; worker < failures.size(); ++worker) {
        queries.emplace_back([&, worker] {
            start.wait();
            try {
                for (int query = 0; query < 100; ++query) {
                    readers.UseConnection([&](LogReader& reader) {
                        const auto rows =
                            worker == 0
                                ? reader.Query({"*"}, {}, 10, 0).results
                                : reader.QueryLogIdRange({"*"}, 0, store.GetCommittedLogId(), 10)
                                      .results;
                        for (const auto& row : rows) {
                            if (row["message"] != row["level"])
                                throw std::runtime_error(
                                    "row decoded with a different file dictionary");
                        }
                    });
                }
            } catch (...) {
                failures[worker] = std::current_exception();
            }
        });
    }
    start.count_down();
    // Expired files are unlinked whenever no read holds the lease, and recreated
    // with fresh dictionaries by the next insert.
    for (int cycle = 0; cycle < 20; ++cycle) {
        store.Maintain();
        const auto first = cycle % 2 == 0 ? "B" : "A";
        const auto second = cycle % 2 == 0 ? "A" : "B";
        EXPECT_EQ(
            store.Insert({Log("2024-01-01T00:00:00Z", first), Log("2024-01-02T00:00:00Z", second)}),
            2);
    }
    queries.clear();  // Join before observing failures or destroying the pool.
    for (const auto& failure : failures) {
        if (failure) {
            try {
                std::rethrow_exception(failure);
            } catch (const std::exception& error) {
                ADD_FAILURE() << error.what();
            }
        }
    }
}

}  // namespace
