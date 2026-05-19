#include <gtest/gtest.h>

#include "config.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"
#include "types.hpp"

#include <barrier>
#include <filesystem>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace loglite;

namespace {

Config make_cfg(const fs::path& tmp) {
    Config cfg;
    cfg.sqlite_dir = tmp;
    cfg.db_path = tmp / "logs.db";
    cfg.log_table_name = "TestLog";
    cfg.log_timestamp_field = "timestamp";
    cfg.auto_rollout = true;
    cfg.sqlite_params["journal_mode"] = "WAL";

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
    cfg.migrations.push_back(m);
    return cfg;
}

}  // namespace

TEST(ReadDatabasePoolTest, ConcurrentReadsAndWriterInserts) {
    auto tmp = fs::temp_directory_path() / "loglite_read_pool_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    auto cfg = make_cfg(tmp);
    WriterDatabase writer{cfg};
    writer.Open();
    writer.Initialize();

    ReadDatabasePool pool{cfg, writer.catalog(), 4};

    std::barrier start{5};

    auto reader = [&](int thread_id) {
        start.arrive_and_wait();
        for (int i = 0; i < 30; ++i) {
            pool.with_connection([&](ReaderDatabase& r) {
                std::vector<QueryFilter> filters{{"level", "=", "INFO"}};
                auto result = r.Query({"message"}, filters, 10, 0);
                EXPECT_GE(result.total, 0);
                (void)thread_id;
            });
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) threads.emplace_back(reader, t);

    start.arrive_and_wait();
    for (int i = 0; i < 50; ++i) {
        writer.Insert({{{"timestamp", "2026-01-01T00:00:00Z"},
                        {"message", std::format("m{}", i)},
                        {"level", "INFO"}}});
    }

    for (auto& th : threads) th.join();

    pool.Close();
    writer.Close();
    fs::remove_all(tmp);
}
