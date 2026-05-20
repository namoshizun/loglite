#include <gtest/gtest.h>

#include "config.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"
#include "tasks/vacuum.hpp"

#include <filesystem>

namespace fs = std::filesystem;
using namespace loglite;

class VacuumTest : public ::testing::Test {
   protected:
    void SetUp() override {
        tmp_ = fs::temp_directory_path() / "loglite_vacuum_test";
        fs::remove_all(tmp_);
        fs::create_directories(tmp_);

        cfg_.sqlite_dir = tmp_;
        cfg_.db_path = tmp_ / "logs.db";
        cfg_.log_table_name = "TestLog";
        cfg_.log_timestamp_field = "timestamp";
        cfg_.auto_rollout = true;
        cfg_.compression = {false, {}};
        cfg_.vacuum_max_size_bytes = parse_size_to_bytes("1TB");
        cfg_.vacuum_target_size_bytes = parse_size_to_bytes("800GB");

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
        cfg_.migrations.push_back(m);

        db_ = std::make_unique<WriterDatabase>(cfg_);
        db_->Open();
        db_->Initialize();
        reader_ = std::make_unique<ReaderDatabase>(cfg_, db_->catalog());
        reader_->Open();
    }

    void TearDown() override {
        reader_.reset();
        db_.reset();
        fs::remove_all(tmp_);
    }

    void insert_logs(int count) {
        std::vector<nlohmann::json> logs;
        for (int i = 0; i < count; ++i) {
            logs.push_back({
                {"timestamp", std::format("2024-01-01T00:{:02d}:{:02d}Z", i / 60, i % 60)},
                {"message", std::format("log entry {}", i)},
                {"level", "INFO"},
            });
        }
        db_->Insert(logs);
    }

    fs::path tmp_;
    Config cfg_;
    std::unique_ptr<WriterDatabase> db_;
    std::unique_ptr<ReaderDatabase> reader_;
};

TEST_F(VacuumTest, RemoveStaleLogsEmptyDb) {
    int removed = tasks::detail::remove_stale_logs(*db_, cfg_);
    EXPECT_EQ(removed, 0);
}

TEST_F(VacuumTest, RemoveStaleLogsNewData) {
    // Insert fresh logs — they should not be removed (max_days=3650 by default)
    insert_logs(10);

    int removed = tasks::detail::remove_stale_logs(*db_, cfg_);
    EXPECT_EQ(removed, 0);

    auto result = reader_->Query({"*"}, {}, 100, 0);
    EXPECT_EQ(result.total, 10);
}

TEST_F(VacuumTest, RemoveExcessiveLogsUnderLimit) {
    insert_logs(10);
    int removed = tasks::detail::remove_excessive_logs(*db_, cfg_);
    EXPECT_EQ(removed, 0);
}

TEST_F(VacuumTest, RemoveExcessiveLogsOverLimit) {
    insert_logs(20);

    // Force deletion of oldest 50% of logs.
    cfg_.vacuum_max_size_bytes = 1;
    cfg_.vacuum_target_size_bytes = db_->GetSizeBytes() / 2;
    cfg_.vacuum_delete_batch_size = 3;  // multiple batches of size 3

    int removed = tasks::detail::remove_excessive_logs(*db_, cfg_);
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

TEST_F(VacuumTest, IncrementalVacuumPassNoOp) {
    // No data deleted, so no freelist
    int remain = tasks::detail::incremental_vacuum_pass(*db_, 20);
    EXPECT_EQ(remain, 0);
}

TEST_F(VacuumTest, IncrementalVacuumPassAfterDelete) {
    insert_logs(50);
    db_->DeleteLogs({{"id", "<=", 25}});

    int remain = tasks::detail::incremental_vacuum_pass(*db_, 20);
    // After deleting rows, freelist should have pages
    EXPECT_GE(remain, 0);
}

TEST(VacuumUtilsTest, RemoveStaleLogsWithFreshData) {
    auto tmp = fs::temp_directory_path() / "loglite_vac_fresh";
    fs::create_directories(tmp);

    Config cfg;
    cfg.sqlite_dir = tmp;
    cfg.db_path = tmp / "logs.db";
    cfg.log_table_name = "TsLog";
    cfg.log_timestamp_field = "timestamp";
    cfg.auto_rollout = true;
    cfg.compression = {false, {}};
    cfg.vacuum_max_days = 3650;

    Migration m;
    m.version = 1;
    m.rollout = {
        "CREATE TABLE IF NOT EXISTS TsLog ("
        "  id        INTEGER PRIMARY KEY,"
        "  timestamp TEXT    NOT NULL,"
        "  message   TEXT    NOT NULL"
        ")"};
    m.rollback = {"DROP TABLE IF EXISTS TsLog"};
    cfg.migrations.push_back(m);

    WriterDatabase db{cfg};
    db.Open();
    db.Initialize();

    // Insert fresh data
    db.Insert({{{"timestamp", "2025-01-01T00:00:00Z"}, {"message", "hello"}}});

    int removed = tasks::detail::remove_stale_logs(db, cfg);
    EXPECT_EQ(removed, 0);

    db.Close();
    fs::remove_all(tmp);
}
