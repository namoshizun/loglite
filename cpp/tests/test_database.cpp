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

        cfg_.sqlite_dir = db_dir_;
        cfg_.db_path = db_dir_ / "logs.db";
        cfg_.log_table_name = "TestLog";
        cfg_.log_timestamp_field = "timestamp";
        cfg_.auto_rollout = true;  // let Initialize() apply migrations
        cfg_.compression = {false, {}};

        Migration m;
        m.version = 1;
        m.rollout = {
            "CREATE TABLE IF NOT EXISTS TestLog ("
            "  id        INTEGER PRIMARY KEY,"
            "  timestamp TEXT    NOT NULL,"
            "  message   TEXT    NOT NULL,"
            "  level     TEXT    NOT NULL,"
            "  service   TEXT"
            ")"};
        m.rollback = {"DROP TABLE IF EXISTS TestLog"};
        cfg_.migrations.push_back(m);

        db_ = std::make_unique<Database>(cfg_);
        db_->Open();
        db_->Initialize();  // creates internal tables + applies migration v1
    }

    void TearDown() override {
        db_.reset();
        fs::remove_all(db_dir_);
    }

    Config cfg_;
    fs::path db_dir_;
    std::unique_ptr<Database> db_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(DatabaseTest, Ping) { EXPECT_TRUE(db_->Ping()); }

TEST_F(DatabaseTest, InsertAndQuery) {
    nlohmann::json log{
        {"timestamp", "2024-01-01T00:00:00"},
        {"message", "hello world"},
        {"level", "INFO"},
        {"service", "test-svc"},
    };
    std::vector<nlohmann::json> logs{log};
    int inserted = db_->Insert(logs);
    EXPECT_EQ(inserted, 1);

    auto result = db_->Query({"*"}, {}, 10, 0);
    EXPECT_EQ(result.total, 1);
    ASSERT_EQ(result.results.size(), 1u);
    EXPECT_EQ(result.results[0]["message"].get<std::string>(), "hello world");
}

TEST_F(DatabaseTest, InsertSkipsRequiredFieldMissing) {
    nlohmann::json bad{{"service", "svc"}};  // missing timestamp + message + level
    std::vector<nlohmann::json> logs{bad};
    int inserted = db_->Insert(logs);
    EXPECT_EQ(inserted, 0);
}

TEST_F(DatabaseTest, InsertBatch) {
    std::vector<nlohmann::json> logs;
    for (int i = 0; i < 5; ++i) {
        logs.push_back({
            {"timestamp", "2024-01-01T00:00:00"},
            {"message", std::format("msg {}", i)},
            {"level", "INFO"},
        });
    }
    EXPECT_EQ(db_->Insert(logs), 5);

    auto result = db_->Query({"*"}, {}, 100, 0);
    EXPECT_EQ(result.total, 5);
}

TEST_F(DatabaseTest, QueryWithEqualFilter) {
    std::vector<nlohmann::json> logs{
        {{"timestamp", "2024-01-01T00:00:00"}, {"message", "a"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-01T00:00:01"}, {"message", "b"}, {"level", "ERROR"}},
    };
    db_->Insert(logs);

    auto result = db_->Query({"*"}, {{"level", "=", "ERROR"}}, 10, 0);
    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.results[0]["level"].get<std::string>(), "ERROR");
}

TEST_F(DatabaseTest, QuerySubstringFilter) {
    std::vector<nlohmann::json> logs{
        {{"timestamp", "2024-01-01T00:00:00"},
         {"message", "connection timeout"},
         {"level", "ERROR"}},
        {{"timestamp", "2024-01-01T00:00:01"}, {"message", "all ok"}, {"level", "INFO"}},
    };
    db_->Insert(logs);

    auto result = db_->Query({"*"}, {{"message", "~=", "timeout"}}, 10, 0);
    EXPECT_EQ(result.total, 1);
    EXPECT_TRUE(result.results[0]["message"].get<std::string>().find("timeout") !=
                std::string::npos);
}

TEST_F(DatabaseTest, QueryPagination) {
    std::vector<nlohmann::json> logs;
    for (int i = 0; i < 10; ++i) {
        logs.push_back({
            {"timestamp", std::format("2024-01-01T00:00:{:02d}", i)},
            {"message", std::format("msg {}", i)},
            {"level", "INFO"},
        });
    }
    db_->Insert(logs);

    auto page1 = db_->Query({"*"}, {}, 5, 0);
    auto page2 = db_->Query({"*"}, {}, 5, 5);
    EXPECT_EQ(page1.total, 10);
    EXPECT_EQ(page1.results.size(), 5u);
    EXPECT_EQ(page2.results.size(), 5u);
}

TEST_F(DatabaseTest, DeleteLogs) {
    std::vector<nlohmann::json> logs{
        {{"timestamp", "2024-01-01T00:00:00"}, {"message", "a"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-01T00:00:01"}, {"message", "b"}, {"level", "ERROR"}},
    };
    db_->Insert(logs);

    int deleted = db_->DeleteLogs({{"level", "=", "ERROR"}});
    EXPECT_EQ(deleted, 1);

    auto result = db_->Query({"*"}, {}, 10, 0);
    EXPECT_EQ(result.total, 1);
}

TEST_F(DatabaseTest, MaxLogId) {
    EXPECT_EQ(db_->GetMaxLogId(), 0);  // empty table

    std::vector<nlohmann::json> logs{
        {{"timestamp", "2024-01-01T00:00:00"}, {"message", "a"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-01T00:00:01"}, {"message", "b"}, {"level", "INFO"}},
    };
    db_->Insert(logs);
    EXPECT_EQ(db_->GetMaxLogId(), 2);
}

TEST_F(DatabaseTest, MigrationRollback) {
    auto versions = db_->GetAppliedVersions();
    EXPECT_TRUE(std::ranges::contains(versions, 1));

    bool ok = db_->RollbackMigration(1, cfg_.migrations[0].rollback);
    EXPECT_TRUE(ok);

    versions = db_->GetAppliedVersions();
    EXPECT_FALSE(std::ranges::contains(versions, 1));
}

TEST_F(DatabaseTest, SizeMb) {
    double sz = db_->GetSizeMB();
    EXPECT_GE(sz, 0.0);
}

TEST(MigrationLoopTest, AllMigrationsAppliedOnInitialize) {
    auto tmp = fs::temp_directory_path() / "loglite_migration_loop_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    Config cfg;
    cfg.sqlite_dir = tmp;
    cfg.db_path = tmp / "logs.db";
    cfg.log_table_name = "TestLog";
    cfg.log_timestamp_field = "timestamp";
    cfg.auto_rollout = true;
    cfg.compression = {false, {}};

    // Three sequential migrations: base table + two ALTER TABLE additions.
    cfg.migrations.push_back({1,
                              {"CREATE TABLE IF NOT EXISTS TestLog ("
                               "  id        INTEGER PRIMARY KEY,"
                               "  timestamp TEXT    NOT NULL,"
                               "  message   TEXT    NOT NULL"
                               ")"},
                              {"DROP TABLE IF EXISTS TestLog"}});
    cfg.migrations.push_back({2, {"ALTER TABLE TestLog ADD COLUMN level TEXT"}, {}});
    cfg.migrations.push_back({3, {"ALTER TABLE TestLog ADD COLUMN service TEXT"}, {}});

    Database db{cfg};
    db.Open();
    db.Initialize();  // must apply v1, v2, v3 — not just v1

    auto versions = db.GetAppliedVersions();
    EXPECT_TRUE(std::ranges::contains(versions, 1)) << "migration v1 not applied";
    EXPECT_TRUE(std::ranges::contains(versions, 2)) << "migration v2 not applied";
    EXPECT_TRUE(std::ranges::contains(versions, 3)) << "migration v3 not applied";

    // Confirm schema reflects all three migrations.
    auto cols = db.GetColumnInfo();
    auto has_col = [&](std::string_view name) {
        return std::ranges::any_of(cols, [&](const ColumnInfo& ci) { return ci.name == name; });
    };
    EXPECT_TRUE(has_col("timestamp")) << "column 'timestamp' missing";
    EXPECT_TRUE(has_col("message")) << "column 'message' missing";
    EXPECT_TRUE(has_col("level")) << "column 'level' missing (migration v2)";
    EXPECT_TRUE(has_col("service")) << "column 'service' missing (migration v3)";

    db.Close();
    fs::remove_all(tmp);
}

// ── SQL-injection prevention ───────────────────────────────────────────────────

// Helper: insert a row so the table is non-empty (Query short-circuits on empty).
static void seed_one_row(Database& db) {
    db.Insert({{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "seed"}, {"level", "INFO"}}});
}

TEST_F(DatabaseTest, QueryRejectsUnknownFieldInSelectList) {
    seed_one_row(*db_);
    EXPECT_THROW(db_->Query({"__injected--field"}, {}, 10, 0), std::runtime_error);
}

TEST_F(DatabaseTest, QueryAllowsWildcard) {
    seed_one_row(*db_);
    EXPECT_NO_THROW(db_->Query({"*"}, {}, 10, 0));
}

TEST_F(DatabaseTest, QueryAllowsKnownFields) {
    seed_one_row(*db_);
    EXPECT_NO_THROW(db_->Query({"id", "timestamp", "message", "level"}, {}, 10, 0));
}

TEST_F(DatabaseTest, QueryRejectsUnknownFieldInFilter) {
    seed_one_row(*db_);
    QueryFilter bad{.field = "'; DROP TABLE TestLog; --", .op = "=", .value = "x"};
    EXPECT_THROW(db_->Query({"*"}, {bad}, 10, 0), std::runtime_error);
}

TEST_F(DatabaseTest, QueryRejectsUnknownOperator) {
    seed_one_row(*db_);
    QueryFilter bad{.field = "level", .op = "LIKE", .value = "%INFO%"};
    EXPECT_THROW(db_->Query({"*"}, {bad}, 10, 0), std::runtime_error);
}

TEST_F(DatabaseTest, DeleteLogsRejectsUnknownField) {
    seed_one_row(*db_);
    QueryFilter bad{.field = "evil_field", .op = "=", .value = "x"};
    EXPECT_THROW(db_->DeleteLogs({bad}), std::runtime_error);
}

TEST_F(DatabaseTest, AllQueryOperatorsAreAccepted) {
    seed_one_row(*db_);
    for (auto op : {"=", "!=", ">", ">=", "<", "<=", "~="}) {
        QueryFilter f{.field = "level", .op = op, .value = "INFO"};
        EXPECT_NO_THROW(db_->Query({"*"}, {f}, 10, 0)) << "operator: " << op;
    }
}
