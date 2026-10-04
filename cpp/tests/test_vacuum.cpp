#include <gtest/gtest.h>

#include "log_store_test_support.hpp"
#include "utils.hpp"

#include <fmt/format.h>

using namespace loglite;

class VacuumTest : public test::LogStoreFixture {
   protected:
    void SetUp() override {
        cfg_.vacuum_max_size_bytes = parse_size_to_bytes("1TB");
        cfg_.vacuum_target_size_bytes = parse_size_to_bytes("800GB");
        cfg_.sqlite_params["auto_vacuum"] = "INCREMENTAL";
        cfg_.vacuum_max_days = 36500;
        LogStoreFixture::SetUp();
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
    db_->Maintain();
    EXPECT_EQ(db_->EstimateLogRowCount(), 0);
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
    db_->Maintain();
    EXPECT_EQ(Query({"message"}, {}, 10, 0).results,
              (std::vector<nlohmann::json>{{{"message", "fresh"}}}));
}

TEST_F(VacuumTest, RemoveExcessiveLogsOverLimit) {
    insert_logs(20);
    db_->Maintain();
    EXPECT_EQ(db_->EstimateLogRowCount(), 20);

    // Force deletion of oldest 50% of logs.
    cfg_.vacuum_max_size_bytes = 1;
    cfg_.vacuum_target_size_bytes = db_->GetSizeBytes() / 2;

    db_->Maintain();

    auto result = Query({"id"}, {}, 100, 0);
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
    cfg_.task_vacuum_max_size = 1;
    db_->Maintain();
    EXPECT_EQ(test::ScalarFile(cfg_.db_path, "PRAGMA freelist_count"), 0);
    std::vector<nlohmann::json> logs;
    // Large rows force real free pages; deleting a few tiny rows may free none.
    for (int i = 0; i < 100; ++i) {
        logs.push_back({{"timestamp", fmt::format("2024-01-01T00:{:02d}:{:02d}Z", i / 60, i % 60)},
                        {"message", std::string(8192, 'x')},
                        {"level", "INFO"}});
    }
    ASSERT_EQ(db_->Insert(logs), 100);
    test::ExecuteFile(cfg_.db_path, "DELETE FROM TestLog WHERE id <= 50");
    const auto before = test::ScalarFile(cfg_.db_path, "PRAGMA freelist_count");
    ASSERT_GT(before, 0);
    db_->Maintain();
    const auto remaining = test::ScalarFile(cfg_.db_path, "PRAGMA freelist_count");
    EXPECT_LT(remaining, before);
    const auto result = Query({"id"}, {}, 100, 0);
    ASSERT_EQ(result.results.size(), 50u);
    for (int i = 0; i < 50; ++i) EXPECT_EQ(result.results[i]["id"], 100 - i);
}
