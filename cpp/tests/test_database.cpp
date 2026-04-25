#include <gtest/gtest.h>

#include "config.hpp"
#include "database.hpp"
#include "migrations.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace loglite;

// ── Fixture ───────────────────────────────────────────────────────────────────

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_dir_ = fs::temp_directory_path() / "loglite_test_db";
        fs::remove_all(db_dir_);
        fs::create_directories(db_dir_);

        cfg_.sqlite_dir          = db_dir_;
        cfg_.db_path             = db_dir_ / "logs.db";
        cfg_.log_table_name      = "TestLog";
        cfg_.log_timestamp_field = "timestamp";
        cfg_.auto_rollout        = true;   // let initialize() apply migrations
        cfg_.compression         = {false, {}};

        Migration m;
        m.version = 1;
        m.rollout = {
            "CREATE TABLE IF NOT EXISTS TestLog ("
            "  id        INTEGER PRIMARY KEY,"
            "  timestamp TEXT    NOT NULL,"
            "  message   TEXT    NOT NULL,"
            "  level     TEXT    NOT NULL,"
            "  service   TEXT"
            ")"
        };
        m.rollback = {"DROP TABLE IF EXISTS TestLog"};
        cfg_.migrations.push_back(m);

        db_ = std::make_unique<Database>(cfg_);
        db_->open();
        db_->initialize(); // creates internal tables + applies migration v1
    }

    void TearDown() override {
        db_.reset();
        fs::remove_all(db_dir_);
    }

    Config                   cfg_;
    fs::path                 db_dir_;
    std::unique_ptr<Database> db_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(DatabaseTest, Ping) {
    EXPECT_TRUE(db_->ping());
}

TEST_F(DatabaseTest, InsertAndQuery) {
    nlohmann::json log{
        {"timestamp", "2024-01-01T00:00:00"},
        {"message",   "hello world"},
        {"level",     "INFO"},
        {"service",   "test-svc"},
    };
    std::vector<nlohmann::json> logs{log};
    int inserted = db_->insert(logs);
    EXPECT_EQ(inserted, 1);

    auto result = db_->query({"*"}, {}, 10, 0);
    EXPECT_EQ(result.total, 1);
    ASSERT_EQ(result.results.size(), 1u);
    EXPECT_EQ(result.results[0]["message"].get<std::string>(), "hello world");
}

TEST_F(DatabaseTest, InsertSkipsRequiredFieldMissing) {
    nlohmann::json bad{{"service", "svc"}}; // missing timestamp + message + level
    std::vector<nlohmann::json> logs{bad};
    int inserted = db_->insert(logs);
    EXPECT_EQ(inserted, 0);
}

TEST_F(DatabaseTest, InsertBatch) {
    std::vector<nlohmann::json> logs;
    for (int i = 0; i < 5; ++i) {
        logs.push_back({
            {"timestamp", "2024-01-01T00:00:00"},
            {"message",   std::format("msg {}", i)},
            {"level",     "INFO"},
        });
    }
    EXPECT_EQ(db_->insert(logs), 5);

    auto result = db_->query({"*"}, {}, 100, 0);
    EXPECT_EQ(result.total, 5);
}

TEST_F(DatabaseTest, QueryWithEqualFilter) {
    std::vector<nlohmann::json> logs{
        {{"timestamp","2024-01-01T00:00:00"},{"message","a"},{"level","INFO"}},
        {{"timestamp","2024-01-01T00:00:01"},{"message","b"},{"level","ERROR"}},
    };
    db_->insert(logs);

    auto result = db_->query({"*"}, {{"level", "=", "ERROR"}}, 10, 0);
    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.results[0]["level"].get<std::string>(), "ERROR");
}

TEST_F(DatabaseTest, QuerySubstringFilter) {
    std::vector<nlohmann::json> logs{
        {{"timestamp","2024-01-01T00:00:00"},{"message","connection timeout"},{"level","ERROR"}},
        {{"timestamp","2024-01-01T00:00:01"},{"message","all ok"},           {"level","INFO"}},
    };
    db_->insert(logs);

    auto result = db_->query({"*"}, {{"message", "~=", "timeout"}}, 10, 0);
    EXPECT_EQ(result.total, 1);
    EXPECT_TRUE(result.results[0]["message"].get<std::string>().find("timeout") != std::string::npos);
}

TEST_F(DatabaseTest, QueryPagination) {
    std::vector<nlohmann::json> logs;
    for (int i = 0; i < 10; ++i) {
        logs.push_back({
            {"timestamp", std::format("2024-01-01T00:00:{:02d}", i)},
            {"message",   std::format("msg {}", i)},
            {"level",     "INFO"},
        });
    }
    db_->insert(logs);

    auto page1 = db_->query({"*"}, {}, 5, 0);
    auto page2 = db_->query({"*"}, {}, 5, 5);
    EXPECT_EQ(page1.total, 10);
    EXPECT_EQ(page1.results.size(), 5u);
    EXPECT_EQ(page2.results.size(), 5u);
}

TEST_F(DatabaseTest, DeleteLogs) {
    std::vector<nlohmann::json> logs{
        {{"timestamp","2024-01-01T00:00:00"},{"message","a"},{"level","INFO"}},
        {{"timestamp","2024-01-01T00:00:01"},{"message","b"},{"level","ERROR"}},
    };
    db_->insert(logs);

    int deleted = db_->delete_logs({{"level", "=", "ERROR"}});
    EXPECT_EQ(deleted, 1);

    auto result = db_->query({"*"}, {}, 10, 0);
    EXPECT_EQ(result.total, 1);
}

TEST_F(DatabaseTest, MaxLogId) {
    EXPECT_EQ(db_->get_max_log_id(), 0); // empty table

    std::vector<nlohmann::json> logs{
        {{"timestamp","2024-01-01T00:00:00"},{"message","a"},{"level","INFO"}},
        {{"timestamp","2024-01-01T00:00:01"},{"message","b"},{"level","INFO"}},
    };
    db_->insert(logs);
    EXPECT_EQ(db_->get_max_log_id(), 2);
}

TEST_F(DatabaseTest, MigrationRollback) {
    auto versions = db_->get_applied_versions();
    EXPECT_TRUE(std::ranges::contains(versions, 1));

    bool ok = db_->rollback_migration(1, cfg_.migrations[0].rollback);
    EXPECT_TRUE(ok);

    versions = db_->get_applied_versions();
    EXPECT_FALSE(std::ranges::contains(versions, 1));
}

TEST_F(DatabaseTest, SizeMb) {
    double sz = db_->get_size_mb();
    EXPECT_GE(sz, 0.0);
}
