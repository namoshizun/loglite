#include <gtest/gtest.h>

#include "test_support.hpp"
#include "reader_pool.hpp"
#include "writer_database.hpp"
#include "types.hpp"

#include <barrier>
#include <fmt/format.h>
#include <thread>
#include <vector>

using namespace loglite;

TEST(ReadDatabasePoolTest, ConcurrentReadsAndWriterInserts) {
    test::TempDirectory directory;
    auto cfg = test::MakeConfig(directory.path());
    cfg.sqlite_params["journal_mode"] = "WAL";
    WriterDatabase writer{cfg};
    writer.Open();
    writer.Initialize();

    ReadDatabasePool pool{cfg, writer.catalog(), 4};

    std::barrier start{5};

    auto reader = [&] {
        int previous_total = 0;
        start.arrive_and_wait();
        for (int i = 0; i < 30; ++i) {
            pool.UseConnection([&](ReaderDatabase& r) {
                std::vector<QueryFilter> filters{{"level", "=", "INFO"}};
                auto result = r.Query({"message"}, filters, 10, 0);
                // COUNT and SELECT use separate snapshots during concurrent inserts.
                // Totals must grow monotonically, but can lag the returned rows.
                EXPECT_GE(result.total, previous_total);
                EXPECT_LE(result.total, 50);
                previous_total = result.total;
                EXPECT_LE(result.results.size(), 10u);
                for (const auto& row : result.results) {
                    EXPECT_EQ(row.size(), 1u);
                    EXPECT_TRUE(row["message"].get<std::string>().starts_with("m"));
                }
            });
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) threads.emplace_back(reader);

    start.arrive_and_wait();
    for (int i = 0; i < 50; ++i) {
        writer.Insert({{{"timestamp", "2026-01-01T00:00:00Z"},
                        {"message", fmt::format("m{}", i)},
                        {"level", "INFO"}}});
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(
        pool.UseConnection([](ReaderDatabase& db) { return db.Query({"*"}, {}, 100, 0).total; }),
        50);
    pool.Close();
    writer.Close();
}
