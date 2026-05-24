#include <gtest/gtest.h>

#include "config.hpp"
#include "migrations.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"
#include "utils.hpp"

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

        db_ = std::make_unique<WriterDatabase>(cfg_);
        db_->Open();
        db_->Initialize();
        reader_ = std::make_unique<ReaderDatabase>(cfg_, db_->catalog());
        reader_->Open();
    }

    void TearDown() override {
        reader_.reset();
        db_.reset();
        fs::remove_all(db_dir_);
    }

    Config cfg_;
    fs::path db_dir_;
    std::unique_ptr<WriterDatabase> db_;
    std::unique_ptr<ReaderDatabase> reader_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(DatabaseTest, Ping) { EXPECT_TRUE(reader_->Ping()); }

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

    auto result = reader_->Query({"*"}, {}, 10, 0);
    EXPECT_EQ(result.total, 1);
    ASSERT_EQ(result.results.size(), 1u);
    EXPECT_EQ(result.results[0]["message"].get<std::string>(), "hello world");
}

TEST_F(DatabaseTest, InsertPreservesFractionalTimestamp) {
    nlohmann::json log{
        {"timestamp", "2024-01-01T00:00:00.123Z"},
        {"message", "with ms"},
        {"level", "INFO"},
    };
    EXPECT_EQ(db_->Insert({log}), 1);

    auto result = reader_->Query({"timestamp"}, {}, 10, 0);
    ASSERT_EQ(result.results.size(), 1u);
    EXPECT_EQ(result.results[0]["timestamp"].get<std::string>(), "2024-01-01T00:00:00.123Z");
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
            {"message", fmt::format("msg {}", i)},
            {"level", "INFO"},
        });
    }
    EXPECT_EQ(db_->Insert(logs), 5);

    auto result = reader_->Query({"*"}, {}, 100, 0);
    EXPECT_EQ(result.total, 5);
}

TEST_F(DatabaseTest, QueryWithEqualFilter) {
    std::vector<nlohmann::json> logs{
        {{"timestamp", "2024-01-01T00:00:00"}, {"message", "a"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-01T00:00:01"}, {"message", "b"}, {"level", "ERROR"}},
    };
    db_->Insert(logs);

    auto result = reader_->Query({"*"}, {{"level", "=", "ERROR"}}, 10, 0);
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

    auto result = reader_->Query({"*"}, {{"message", "~=", "timeout"}}, 10, 0);
    EXPECT_EQ(result.total, 1);
    EXPECT_TRUE(result.results[0]["message"].get<std::string>().find("timeout") !=
                std::string::npos);
}

TEST_F(DatabaseTest, QueryPagination) {
    std::vector<nlohmann::json> logs;
    for (int i = 0; i < 10; ++i) {
        logs.push_back({
            {"timestamp", fmt::format("2024-01-01T00:00:{:02d}", i)},
            {"message", fmt::format("msg {}", i)},
            {"level", "INFO"},
        });
    }
    db_->Insert(logs);

    auto page1 = reader_->Query({"*"}, {}, 5, 0);
    auto page2 = reader_->Query({"*"}, {}, 5, 5);
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

    auto result = reader_->Query({"*"}, {}, 10, 0);
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
    EXPECT_TRUE(range_contains(versions, 1));

    bool ok = db_->RollbackMigration(1, cfg_.migrations[0].rollback);
    EXPECT_TRUE(ok);

    versions = db_->GetAppliedVersions();
    EXPECT_FALSE(range_contains(versions, 1));
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

    WriterDatabase db{cfg};
    db.Open();
    db.Initialize();  // must apply v1, v2, v3 — not just v1

    auto versions = db.GetAppliedVersions();
    EXPECT_TRUE(range_contains(versions, 1)) << "migration v1 not applied";
    EXPECT_TRUE(range_contains(versions, 2)) << "migration v2 not applied";
    EXPECT_TRUE(range_contains(versions, 3)) << "migration v3 not applied";

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
static void seed_one_row(WriterDatabase& db) {
    db.Insert({{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "seed"}, {"level", "INFO"}}});
}

TEST_F(DatabaseTest, QueryRejectsUnknownFieldInSelectList) {
    seed_one_row(*db_);
    EXPECT_THROW(reader_->Query({"__injected--field"}, {}, 10, 0), std::runtime_error);
}

TEST_F(DatabaseTest, QueryAllowsWildcard) {
    seed_one_row(*db_);
    EXPECT_NO_THROW(reader_->Query({"*"}, {}, 10, 0));
}

TEST_F(DatabaseTest, QueryAllowsKnownFields) {
    seed_one_row(*db_);
    EXPECT_NO_THROW(reader_->Query({"id", "timestamp", "message", "level"}, {}, 10, 0));
}

TEST_F(DatabaseTest, QueryRejectsUnknownFieldInFilter) {
    seed_one_row(*db_);
    QueryFilter bad{.field = "'; DROP TABLE TestLog; --", .op = "=", .value = "x"};
    EXPECT_THROW(reader_->Query({"*"}, {bad}, 10, 0), std::runtime_error);
}

TEST_F(DatabaseTest, QueryRejectsUnknownOperator) {
    seed_one_row(*db_);
    QueryFilter bad{.field = "level", .op = "LIKE", .value = "%INFO%"};
    EXPECT_THROW(reader_->Query({"*"}, {bad}, 10, 0), std::runtime_error);
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
        EXPECT_NO_THROW(reader_->Query({"*"}, {f}, 10, 0)) << "operator: " << op;
    }
}

// ── PRAGMAs / health ───────────────────────────────────────────────────────

TEST_F(DatabaseTest, GetPragma) {
    auto val = db_->GetPragma("journal_mode");
    EXPECT_FALSE(val.empty());
}

TEST_F(DatabaseTest, SetPragma) {
    db_->SetPragma("cache_size", "-4000");
    auto val = db_->GetPragma("cache_size");
    EXPECT_EQ(val, "-4000");
}

TEST_F(DatabaseTest, GetPragmaPageCount) {
    auto val = db_->GetPragma("page_count");
    EXPECT_FALSE(val.empty());
    EXPECT_NE(val, "0");
}

TEST_F(DatabaseTest, GetPragmaPageSize) {
    auto val = db_->GetPragma("page_size");
    EXPECT_FALSE(val.empty());
    int64_t sz = std::stoll(val);
    EXPECT_GT(sz, 0);
}

TEST_F(DatabaseTest, IncrementalVacuumNoOp) { EXPECT_NO_THROW(db_->IncrementalVacuum(0)); }

TEST_F(DatabaseTest, GetMinLogId) {
    EXPECT_EQ(db_->GetMinLogId(), 0);

    db_->Insert({{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "a"}, {"level", "INFO"}}});
    EXPECT_EQ(db_->GetMinLogId(), 1);
}

TEST_F(DatabaseTest, GetMinTimestamp) {
    auto ts = db_->GetMinTimestamp();
    EXPECT_TRUE(ts.empty());

    db_->Insert({{{"timestamp", "2024-06-01T00:00:00Z"}, {"message", "a"}, {"level", "INFO"}}});
    db_->Insert({{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "b"}, {"level", "INFO"}}});

    auto min_ts = db_->GetMinTimestamp();
    EXPECT_FALSE(min_ts.empty());
}

TEST_F(DatabaseTest, RefreshColumnInfo) {
    db_->RefreshColumnInfo();
    auto cols = db_->GetColumnInfo();
    EXPECT_FALSE(cols.empty());

    bool has_id = false, has_ts = false, has_msg = false;
    for (const auto& ci : cols) {
        if (ci.name == "id") has_id = true;
        if (ci.name == "timestamp") has_ts = true;
        if (ci.name == "message") has_msg = true;
    }
    EXPECT_TRUE(has_id);
    EXPECT_TRUE(has_ts);
    EXPECT_TRUE(has_msg);
}

TEST_F(DatabaseTest, WALCheckpoint) { EXPECT_NO_THROW(db_->WALCheckpoint("PASSIVE")); }

TEST_F(DatabaseTest, GetColumnDictRowsInitiallyEmpty) {
    auto rows = db_->GetColumnDictRows();
    EXPECT_TRUE(rows.empty());
}

TEST_F(DatabaseTest, InsertColumnDictValue) {
    bool ok = db_->InsertColumnDictValue("level", "INFO", 1);
    EXPECT_TRUE(ok);

    auto rows = db_->GetColumnDictRows();
    EXPECT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<0>(rows[0]), "level");
    EXPECT_EQ(std::get<1>(rows[0]), "INFO");
    EXPECT_EQ(std::get<2>(rows[0]), 1);
}

TEST_F(DatabaseTest, DeleteLogsById) {
    db_->Insert({{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "keep"}, {"level", "INFO"}}});
    db_->Insert({{{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "del"}, {"level", "INFO"}}});

    int deleted = db_->DeleteLogs({{"id", "=", 2}});
    EXPECT_EQ(deleted, 1);

    auto result = reader_->Query({"*"}, {}, 10, 0);
    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.results[0]["message"], "keep");
}

TEST_F(DatabaseTest, CreateInternalTablesIdempotent) {
    EXPECT_NO_THROW(db_->CreateInternalTables());
    EXPECT_NO_THROW(db_->CreateInternalTables());
}

TEST_F(DatabaseTest, InsertAndPruneStatsRows) {
    ActivityStatsRow old_activity;
    old_activity.since = "2024-01-01T00:00:00Z";
    old_activity.until = "2024-01-01T00:01:00Z";
    old_activity.query_count = 1;

    ActivityStatsRow new_activity;
    new_activity.since = "2024-01-02T00:00:00Z";
    new_activity.until = "2024-01-02T00:01:00Z";
    new_activity.query_count = 2;
    DatabaseStatsRow old_db{"2024-01-01T00:01:00Z", 10, 4096};
    DatabaseStatsRow new_db{"2024-01-02T00:01:00Z", 20, 8192};

    EXPECT_TRUE(db_->InsertActivityStats(old_activity));
    EXPECT_TRUE(db_->InsertActivityStats(new_activity));
    EXPECT_TRUE(db_->InsertDatabaseStats(old_db));
    EXPECT_TRUE(db_->InsertDatabaseStats(new_db));

    EXPECT_EQ(db_->DeleteStatsBefore("2024-01-02T00:00:00Z"), 2);
    EXPECT_EQ(db_->DeleteStatsBefore("2024-01-03T00:00:00Z"), 2);
}
