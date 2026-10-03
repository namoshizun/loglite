#include <gtest/gtest.h>

#include "config.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace asio = boost::asio;
using namespace loglite;
using namespace std::chrono_literals;

class AsyncDatabaseTest : public ::testing::Test {
   protected:
    void SetUp() override {
        tmp_ = fs::temp_directory_path() / "loglite_async_database_test";
        fs::remove_all(tmp_);
        fs::create_directories(tmp_);
        cfg_.db_path = tmp_ / "logs.db";
        cfg_.auto_rollout = true;
        cfg_.sqlite_params["journal_mode"] = "WAL";
        cfg_.migrations = {{1,
                            {"CREATE TABLE Log (id INTEGER PRIMARY KEY, timestamp TEXT, "
                             "message TEXT)"},
                            {"DROP TABLE Log"}}};
        writer_ = std::make_unique<WriterDatabase>(cfg_);
        writer_->Open();
        writer_->Initialize();
        readers_ = std::make_unique<ReadDatabasePool>(cfg_, writer_->catalog(), 1);
    }

    void TearDown() override {
        caller_pool_.join();
        reader_pool_.join();
        writer_pool_.join();
        readers_.reset();
        writer_.reset();
        fs::remove_all(tmp_);
    }

    asio::awaitable<void> CheckReaderExecutor() {
        const auto caller_id = std::this_thread::get_id();
        auto worker_id = co_await readers_->AsyncUseConnection(
            reader_pool_.get_executor(), [&](ReaderDatabase& db) {
                EXPECT_TRUE(reader_pool_.get_executor().running_in_this_thread());
                EXPECT_TRUE(db.Ping());
                return std::this_thread::get_id();
            });
        EXPECT_NE(worker_id, caller_id);
        EXPECT_EQ(std::this_thread::get_id(), caller_id);
        EXPECT_TRUE(caller_pool_.get_executor().running_in_this_thread());
    }

    asio::awaitable<void> CheckWriterExecutor() {
        const auto caller_id = std::this_thread::get_id();
        co_await writer_->AsyncUseConnection(write_strand_, [&](WriterDatabase& db) {
            EXPECT_TRUE(write_strand_.running_in_this_thread());
            EXPECT_NE(std::this_thread::get_id(), caller_id);
            db.SetPragma("cache_size", "-100");
        });
        EXPECT_EQ(std::this_thread::get_id(), caller_id);

        // The lambda is destroyed before the operation is awaited. Its move-only
        // capture must be owned by the asynchronous operation.
        auto operation = writer_->AsyncUseConnection(
            write_strand_, [this, value = std::make_unique<int>(42)](WriterDatabase& db) mutable {
                EXPECT_TRUE(write_strand_.running_in_this_thread());
                EXPECT_EQ(db.GetPragma("cache_size"), "-100");
                return std::move(value);
            });
        auto value = co_await std::move(operation);
        EXPECT_NE(value, nullptr);
        if (value) EXPECT_EQ(*value, 42);
        EXPECT_EQ(std::this_thread::get_id(), caller_id);
    }

    asio::awaitable<void> CheckExceptions() {
        try {
            co_await readers_->AsyncUseConnection(
                reader_pool_.get_executor(),
                [](ReaderDatabase&) -> int { throw std::runtime_error("reader failure"); });
            ADD_FAILURE() << "Reader exception was not propagated";
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "reader failure");
            EXPECT_TRUE(caller_pool_.get_executor().running_in_this_thread());
        }

        // An exception must release the connection lease so later work can proceed.
        co_await readers_->AsyncUseConnection(reader_pool_.get_executor(),
                                              [](ReaderDatabase& db) { EXPECT_TRUE(db.Ping()); });

        try {
            co_await writer_->AsyncUseConnection(
                write_strand_, [](WriterDatabase&) { throw std::runtime_error("writer failure"); });
            ADD_FAILURE() << "Writer exception was not propagated";
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "writer failure");
            EXPECT_TRUE(caller_pool_.get_executor().running_in_this_thread());
        }
    }

    asio::awaitable<int> SerializedWrite(std::atomic<int>& active) {
        co_return co_await writer_->AsyncUseConnection(write_strand_, [&](WriterDatabase& db) {
            EXPECT_TRUE(write_strand_.running_in_this_thread());
            EXPECT_EQ(active.fetch_add(1), 0);
            std::this_thread::sleep_for(5ms);
            auto inserted = db.Insert({{{"message", "serialized"}}});
            EXPECT_EQ(active.fetch_sub(1), 1);
            return inserted;
        });
    }

    asio::awaitable<bool> BlockedRead(std::promise<void>& entered,
                                      const std::shared_future<void>& release) {
        co_return co_await readers_->AsyncUseConnection(reader_pool_.get_executor(),
                                                        [&](ReaderDatabase& db) {
                                                            entered.set_value();
                                                            release.wait();
                                                            return db.Ping();
                                                        });
    }

    fs::path tmp_;
    Config cfg_;
    std::unique_ptr<WriterDatabase> writer_;
    std::unique_ptr<ReadDatabasePool> readers_;
    asio::thread_pool caller_pool_{1};
    asio::thread_pool reader_pool_{2};
    asio::thread_pool writer_pool_{4};
    asio::strand<asio::thread_pool::executor_type> write_strand_{
        asio::make_strand(writer_pool_.get_executor())};
};

TEST_F(AsyncDatabaseTest, ReaderRunsOnWorkerAndResumesCaller) {
    asio::co_spawn(caller_pool_, CheckReaderExecutor(), asio::use_future).get();
}

TEST_F(AsyncDatabaseTest, WriterSupportsVoidAndOwnedMoveOnlyCallables) {
    asio::co_spawn(caller_pool_, CheckWriterExecutor(), asio::use_future).get();
}

TEST_F(AsyncDatabaseTest, ExceptionsResumeCallerAndReleaseReaderLease) {
    asio::co_spawn(caller_pool_, CheckExceptions(), asio::use_future).get();
}

TEST_F(AsyncDatabaseTest, WriterStrandSerializesConcurrentOperations) {
    std::atomic<int> active{0};
    std::vector<std::future<int>> results;
    for (int i = 0; i < 16; ++i) {
        results.push_back(asio::co_spawn(caller_pool_, SerializedWrite(active), asio::use_future));
    }
    for (auto& result : results) EXPECT_EQ(result.get(), 1);
    EXPECT_EQ(active.load(), 0);
    EXPECT_EQ(readers_->UseConnection(
                  [](ReaderDatabase& db) { return db.Query({"*"}, {}, 100, 0).results.size(); }),
              16u);
}

TEST_F(AsyncDatabaseTest, BlockedDatabaseWorkLeavesCallerResponsive) {
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    auto result =
        asio::co_spawn(caller_pool_, BlockedRead(entered, release_future), asio::use_future);
    EXPECT_EQ(entered_future.wait_for(2s), std::future_status::ready);

    std::promise<void> heartbeat;
    auto heartbeat_future = heartbeat.get_future();
    asio::post(caller_pool_, [&] { heartbeat.set_value(); });
    const auto heartbeat_status = heartbeat_future.wait_for(2s);
    release.set_value();

    EXPECT_TRUE(result.get());
    heartbeat_future.get();
    EXPECT_EQ(heartbeat_status, std::future_status::ready);
}
