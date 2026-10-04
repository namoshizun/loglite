#include <gtest/gtest.h>

#include "test_support.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"
#include "tasks/vacuum.hpp"

#include <fmt/format.h>

using namespace loglite;

class VacuumTest : public test::DatabaseFixture {
   protected:
    void SetUp() override {
        cfg_.vacuum_max_size_bytes = parse_size_to_bytes("1TB");
        cfg_.vacuum_target_size_bytes = parse_size_to_bytes("800GB");
        cfg_.sqlite_params["auto_vacuum"] = "INCREMENTAL";
        DatabaseFixture::SetUp();
    }

    void insert_logs(int count) {
        std::vector<nlohmann::json> logs;
        for (int i = 0; i < count; ++i) {
            logs.push_back({
                {"timestamp", fmt::format("2024-01-01T00:{:02d}:{:02d}Z", i / 60, i % 60)},
                {"message", fmt::format("log entry {}", i)},
                {"level", "INFO"},
            });
        }
        db_->Insert(logs);
    }
};

TEST_F(VacuumTest, RetentionDeletesExpiredRowsAndPreservesFreshOnes) {
    cfg_.vacuum_max_days = 1;
    EXPECT_EQ(tasks::detail::remove_stale_logs(*db_, cfg_, 0), 0);
    const auto now = std::chrono::system_clock::now();
    ASSERT_EQ(db_->Insert({
                  {{"timestamp", format_utc(now - std::chrono::hours{72})},
                   {"message", "expired"},
                   {"level", "INFO"}},
                  {{"timestamp", format_utc(now - std::chrono::hours{1})},
                   {"message", "fresh"},
                   {"level", "INFO"}},
              }),
              2);
    EXPECT_EQ(tasks::detail::remove_stale_logs(*db_, cfg_, 0), 1);
    EXPECT_EQ(reader_->Query({"message"}, {}, 10, 0).results,
              (std::vector<nlohmann::json>{{{"message", "fresh"}}}));
}

TEST_F(VacuumTest, RemoveExcessiveLogsOverLimit) {
    insert_logs(20);
    EXPECT_EQ(tasks::detail::remove_excessive_logs(*db_, cfg_, 0), 0);

    // Force deletion of oldest 50% of logs.
    cfg_.vacuum_max_size_bytes = 1;
    cfg_.vacuum_target_size_bytes = db_->GetSizeBytes() / 2;

    int removed = tasks::detail::remove_excessive_logs(*db_, cfg_, 0);
    EXPECT_EQ(removed, 10);

    auto result = reader_->Query({"id"}, {}, 100, 0);
    EXPECT_EQ(result.total, 10);

    // Verify remaining IDs are exactly 11 to 20 (continuous, no Swiss-cheese holes).
    std::vector<int> remaining_ids;
    for (const auto& log : result.results) {
        remaining_ids.push_back(log["id"].get<int>());
    }
    std::vector<int> expected_ids = {20, 19, 18, 17, 16, 15, 14, 13, 12, 11};
    EXPECT_EQ(remaining_ids, expected_ids);
}

TEST_F(VacuumTest, IncrementalVacuumReclaimsFreePagesWithoutDeletingLiveRows) {
    EXPECT_EQ(tasks::detail::incremental_vacuum_pass(*db_, 1), 0);
    std::vector<nlohmann::json> logs;
    // Large rows force real free pages; deleting a few tiny rows may free none.
    for (int i = 0; i < 100; ++i) {
        logs.push_back({{"timestamp", fmt::format("2024-01-01T00:{:02d}:{:02d}Z", i / 60, i % 60)},
                        {"message", std::string(8192, 'x')},
                        {"level", "INFO"}});
    }
    ASSERT_EQ(db_->Insert(logs), 100);
    ASSERT_EQ(db_->DeleteLogs({{"id", "<=", 50}}), 50);
    const auto before = std::stoll(db_->GetPragma("freelist_count"));
    ASSERT_GT(before, 0);
    const auto remaining = tasks::detail::incremental_vacuum_pass(*db_, 1);
    EXPECT_LT(remaining, before);
    const auto result = reader_->Query({"id"}, {}, 100, 0);
    ASSERT_EQ(result.results.size(), 50u);
    for (int i = 0; i < 50; ++i) EXPECT_EQ(result.results[i]["id"], 100 - i);
}
