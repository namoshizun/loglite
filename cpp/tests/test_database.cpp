#include <gtest/gtest.h>

#include "test_support.hpp"
#include "backlog.hpp"
#include "migrations.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"
#include "utils.hpp"

#include <memory>

using namespace loglite;

// ── Fixture ───────────────────────────────────────────────────────────────────

class DatabaseTest : public test::DatabaseFixture {
   protected:
    auto OpenPeer() {
        sqlite3* raw = nullptr;
        const int rc = sqlite3_open(cfg_.db_path.c_str(), &raw);
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> peer{raw, sqlite3_close};
        if (rc != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(raw));
        return peer;
    }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(DatabaseTest, InsertRoundTripsValidRowsAndSkipsMissingRequiredFields) {
    const std::vector<nlohmann::json> logs{
        {{"timestamp", "2024-01-01T00:00:00.123Z"},
         {"message", "hello world"},
         {"level", "INFO"},
         {"service", "test-svc"}},
        {{"service", "svc"}},  // missing timestamp + message + level
        {{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "second"}, {"level", "ERROR"}},
    };
    ASSERT_EQ(db_->Insert(logs), 2);
    auto result = reader_->Query({"*"}, {}, 10, 0);
    EXPECT_EQ(result.total, 2);
    ASSERT_EQ(result.results.size(), 2u);
    auto first = logs[0];
    first["id"] = 1;
    auto second = logs[2];
    second["id"] = 2;
    second["service"] = nullptr;
    EXPECT_EQ(result.results, (std::vector<nlohmann::json>{second, first}));
}

TEST_F(DatabaseTest, FailedInsertRollsBackEntireBatchAndRestoresBacklog) {
    db_->ApplyMigration(0, {"ALTER TABLE TestLog DROP COLUMN service",
                            "ALTER TABLE TestLog ADD COLUMN service INTEGER"});
    cfg_.compression = {true, {"service"}};
    OpenDatabase();
    db_->ApplyMigration(
        2, {"CREATE TRIGGER reject_log BEFORE INSERT ON TestLog WHEN NEW.message = 'reject' "
            "BEGIN SELECT RAISE(ABORT, 'injected write failure'); END"});

    Backlog backlog{10};
    for (const auto* message : {"accepted", "reject"}) {
        backlog.Add({{"timestamp", "2024-01-01T00:00:00Z"},
                     {"message", message},
                     {"level", "INFO"},
                     {"service", "new-service"}});
    }
    auto persist = [&](const auto& entries) { return db_->Insert(entries); };
    EXPECT_THROW(backlog.Flush(persist), std::runtime_error);
    EXPECT_EQ(backlog.Size(), 2u);
    EXPECT_EQ(db_->EstimateLogRowCount(), 0);
    EXPECT_TRUE(db_->GetColumnDictRows().empty());

    db_->RollbackMigration(2, {"DROP TRIGGER reject_log"});
    EXPECT_EQ(backlog.Flush(persist), 2);
    EXPECT_EQ(backlog.Size(), 0u);
    EXPECT_EQ(db_->GetColumnDictRows().size(), 1u);
    // Reopening must decode from the persisted dictionary, not stale cache entries.
    reader_.reset();
    db_->Initialize();
    reader_ = std::make_unique<ReaderDatabase>(cfg_, db_->catalog());
    reader_->Open();
    auto result = reader_->Query({"*"}, {}, 10, 0);
    ASSERT_EQ(result.results.size(), 2u);
    for (const auto& row : result.results) EXPECT_EQ(row["service"], "new-service");
}

TEST_F(DatabaseTest, FailedCommitRestoresBacklogAndCanBeRetried) {
    auto peer = OpenPeer();
    Statement{peer.get(), "BEGIN"}.Step();
    Statement{peer.get(), "SELECT * FROM TestLog"}.Step();
    Backlog backlog{10};
    backlog.Add({{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "retry"}, {"level", "INFO"}});
    auto persist = [&](const auto& entries) { return db_->Insert(entries); };

    EXPECT_THROW(backlog.Flush(persist), std::runtime_error);
    EXPECT_EQ(backlog.Size(), 1u);
    EXPECT_EQ(db_->EstimateLogRowCount(), 0);
    Statement{peer.get(), "COMMIT"}.Step();
    EXPECT_EQ(backlog.Flush(persist), 1);
    EXPECT_EQ(reader_->Query({"*"}, {}, 10, 0).results.size(), 1u);
}

TEST_F(DatabaseTest, LockedReadsThrowInsteadOfReturningEmptyResults) {
    db_->Insert(
        {{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "existing"}, {"level", "INFO"}}});
    // Populate SQLite's schema cache so statement preparation succeeds under the lock.
    ASSERT_EQ(reader_->Query({"*"}, {}, 10, 0).total, 1);
    auto peer = OpenPeer();
    Statement{peer.get(), "BEGIN EXCLUSIVE"}.Step();

    EXPECT_THROW(reader_->Query({"*"}, {}, 10, 0), std::runtime_error);
    EXPECT_THROW(reader_->Query({"*"}, {{"level", "=", "INFO"}}, 10, 0), std::runtime_error);
    EXPECT_THROW(reader_->QueryActivityStats("2024", "2025", {"*"}, "asc"), std::runtime_error);
    EXPECT_THROW(reader_->QueryDatabaseStats("2024", "2025", {"*"}, "asc"), std::runtime_error);
    EXPECT_THROW((void)reader_->GetMaxLogId(), std::runtime_error);
    EXPECT_THROW((void)reader_->GetMinLogId(), std::runtime_error);
    EXPECT_THROW((void)reader_->GetMinTimestamp(), std::runtime_error);
    EXPECT_THROW((void)reader_->GetPragma("page_count"), std::runtime_error);
    EXPECT_THROW((void)reader_->FetchTableColumns("TestLog"), std::runtime_error);
    EXPECT_THROW(db_->GetAppliedVersions(), std::runtime_error);
    EXPECT_THROW(db_->GetColumnDictRows(), std::runtime_error);

    Statement{peer.get(), "ROLLBACK"}.Step();
    EXPECT_EQ(reader_->Query({"*"}, {}, 10, 0).total, 1);
}

TEST_F(DatabaseTest, ReadErrorAfterFirstRowThrowsInsteadOfReturningPartialResults) {
    db_->Insert({
        {{"timestamp", "2024-01-02"}, {"message", "first"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-01"}, {"message", "second"}, {"level", "INFO"}},
    });
    db_->ApplyMigration(2, {
                               "ALTER TABLE TestLog RENAME TO RawLog",
                               "CREATE INDEX raw_timestamp ON RawLog(timestamp DESC)",
                               "CREATE VIEW TestLog AS SELECT id, timestamp, CASE WHEN id = 2 "
                               "THEN abs(-9223372036854775808) ELSE message END AS message, level, "
                               "service FROM RawLog",
                           });
    auto peer = OpenPeer();
    Statement probe{peer.get(), "SELECT message FROM TestLog ORDER BY timestamp DESC"};
    ASSERT_EQ(sqlite3_step(probe), SQLITE_ROW);
    ASSERT_EQ(sqlite3_step(probe), SQLITE_ERROR);
    EXPECT_THROW(reader_->Query({"*"}, {}, 10, 0), std::runtime_error);
}

TEST_F(DatabaseTest, QueryOperatorsSelectExpectedRows) {
    ASSERT_EQ(
        db_->Insert({
            {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "alpha"}, {"level", "INFO"}},
            {{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "beta"}, {"level", "ERROR"}},
            {{"timestamp", "2024-01-01T00:00:02Z"}, {"message", "alphabet"}, {"level", "INFO"}},
        }),
        3);
    struct Case {
        const char* op;
        const char* value;
        std::vector<int> ids;
    };
    const Case cases[]{
        {"=", "alpha", {1}},        {"!=", "alpha", {3, 2}},     {">", "alpha", {3, 2}},
        {">=", "alpha", {3, 2, 1}}, {"<", "alphabet", {1}},      {"<=", "alphabet", {3, 1}},
        {"~=", "alpha", {3, 1}},    {"~=", "alphabetextra", {}}, {"=", "x' OR 1=1 --", {}},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(std::string{c.op} + c.value);
        auto result = reader_->Query({"id"}, {{"message", c.op, c.value}}, 10, 0);
        EXPECT_EQ(result.total, c.ids.size());
        std::vector<int> ids;
        for (const auto& row : result.results) ids.push_back(row["id"]);
        EXPECT_EQ(ids, c.ids);
    }
}

TEST_F(DatabaseTest, PaginationKeepsOrderingAndTotalAcrossPages) {
    std::vector<nlohmann::json> logs;
    for (int i = 0; i < 10; ++i) {
        logs.push_back({{"timestamp", fmt::format("2024-01-01T00:00:{:02d}Z", i)},
                        {"message", fmt::format("msg {}", i)},
                        {"level", "INFO"}});
    }
    ASSERT_EQ(db_->Insert(logs), 10);
    struct Case {
        int limit, offset;
        std::vector<int> ids;
    };
    const Case cases[]{
        {5, 0, {10, 9, 8, 7, 6}}, {5, 5, {5, 4, 3, 2, 1}}, {5, 10, {}}, {3, 2, {8, 7, 6}}};
    for (const auto& c : cases) {
        SCOPED_TRACE(c.offset);
        auto page = reader_->Query({"id"}, {}, c.limit, c.offset);
        EXPECT_EQ(page.total, 10);
        std::vector<int> ids;
        for (const auto& row : page.results) {
            EXPECT_EQ(row.size(), 1u);  // projection must not leak other columns
            ids.push_back(row["id"]);
        }
        EXPECT_EQ(ids, c.ids);
    }
}

TEST_F(DatabaseTest, DeletionPreservesUnmatchedRows) {
    ASSERT_EQ(
        db_->Insert({
            {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "keep"}, {"level", "INFO"}},
            {{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "error"}, {"level", "ERROR"}},
            {{"timestamp", "2024-01-01T00:00:02Z"}, {"message", "delete by id"}, {"level", "INFO"}},
        }),
        3);
    EXPECT_EQ(db_->DeleteLogs({{"level", "=", "ERROR"}}), 1);
    auto rows = reader_->Query({"id"}, {}, 10, 0).results;
    EXPECT_EQ(rows, (std::vector<nlohmann::json>{{{"id", 3}}, {{"id", 1}}}));
    EXPECT_EQ(db_->DeleteLogs({{"id", "=", 3}}), 1);
    EXPECT_EQ(db_->DeleteLogs({{"id", "=", 99}}), 0);
    rows = reader_->Query({"message"}, {}, 10, 0).results;
    EXPECT_EQ(rows, (std::vector<nlohmann::json>{{{"message", "keep"}}}));
}

TEST_F(DatabaseTest, LogBoundsTrackEmptyAndPopulatedTables) {
    EXPECT_EQ(db_->GetMaxLogId(), 0);  // empty table
    EXPECT_EQ(db_->GetMinLogId(), 0);
    EXPECT_TRUE(db_->GetMinTimestamp().empty());
    ASSERT_EQ(db_->Insert({
                  {{"timestamp", "2024-06-01T00:00:00Z"}, {"message", "newer"}, {"level", "INFO"}},
                  {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "older"}, {"level", "INFO"}},
              }),
              2);
    EXPECT_EQ(db_->GetMaxLogId(), 2);
    EXPECT_EQ(db_->GetMinLogId(), 1);
    EXPECT_EQ(db_->GetMinTimestamp(), "2024-01-01T00:00:00Z");
}

// ── SQL-injection prevention ───────────────────────────────────────────────────

TEST_F(DatabaseTest, UnsafeIdentifiersAndOperatorsAreRejectedWithoutChangingData) {
    // Insert a row so the table is non-empty (Query short-circuits on empty).
    ASSERT_EQ(
        db_->Insert(
            {{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "seed"}, {"level", "INFO"}}}),
        1);
    EXPECT_THROW(reader_->Query({"__injected--field"}, {}, 10, 0), std::runtime_error);
    const QueryFilter cases[]{
        {"'; DROP TABLE TestLog; --", "=", "x"},
        {"evil_field", "=", "x"},
        {"level", "LIKE", "%INFO%"},
    };
    for (const auto& filter : cases) {
        SCOPED_TRACE(filter.field + ":" + filter.op);
        EXPECT_THROW(reader_->Query({"*"}, {filter}, 10, 0), std::runtime_error);
        EXPECT_THROW(db_->DeleteLogs({filter}), std::runtime_error);
    }
    EXPECT_EQ(reader_->Query({"message"}, {}, 10, 0).results,
              (std::vector<nlohmann::json>{{{"message", "seed"}}}));
}

// ── PRAGMAs / health ───────────────────────────────────────────────────────

TEST_F(DatabaseTest, ColumnMetadataReflectsCommittedSchemaChanges) {
    ASSERT_TRUE(db_->ApplyMigration(
        2, {"ALTER TABLE TestLog ADD COLUMN source INTEGER NOT NULL DEFAULT 7"}));
    db_->RefreshColumnInfo();
    const auto columns = db_->GetColumnInfo();
    const auto source = std::ranges::find(columns, "source", &ColumnInfo::name);
    ASSERT_NE(source, columns.end());
    EXPECT_EQ(source->type, "INTEGER");
    EXPECT_TRUE(source->not_null);
    EXPECT_FALSE(source->is_pk);
    const auto id = std::ranges::find(columns, "id", &ColumnInfo::name);
    ASSERT_NE(id, columns.end());
    EXPECT_TRUE(id->is_pk);
}

TEST_F(DatabaseTest, DictionaryRowsRoundTripColumnValueAndId) {
    EXPECT_TRUE(db_->GetColumnDictRows().empty());
    ASSERT_TRUE(db_->InsertColumnDictValue("level", "INFO", 1));
    ASSERT_TRUE(db_->InsertColumnDictValue("service", "auth", 1));
    EXPECT_EQ(db_->GetColumnDictRows(), (std::vector<std::tuple<std::string, std::string, ValueId>>{
                                            {"level", "INFO", 1}, {"service", "auth", 1}}));
}
