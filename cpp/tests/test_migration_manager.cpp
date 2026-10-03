#include <gtest/gtest.h>

#include "test_support.hpp"
#include "writer_database.hpp"
#include "migrations.hpp"
#include "utils.hpp"

using namespace loglite;

class MigrationManagerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        cfg_ = test::MakeConfig(directory_.path());
        cfg_.auto_rollout = false;
        cfg_.migrations.clear();

        Migration m1;
        m1.version = 1;
        m1.rollout = {
            "CREATE TABLE IF NOT EXISTS TestLog ("
            "  id        INTEGER PRIMARY KEY,"
            "  timestamp TEXT    NOT NULL,"
            "  message   TEXT    NOT NULL"
            ")"};
        m1.rollback = {"DROP TABLE IF EXISTS TestLog"};
        cfg_.migrations.push_back(m1);

        Migration m2;
        m2.version = 2;
        m2.rollout = {"ALTER TABLE TestLog ADD COLUMN level TEXT NOT NULL DEFAULT 'INFO'"};
        m2.rollback = {};
        cfg_.migrations.push_back(m2);

        Migration m3;
        m3.version = 5;
        m3.rollout = {"ALTER TABLE TestLog ADD COLUMN service TEXT"};
        m3.rollback = {};
        cfg_.migrations.push_back(m3);

        db_ = std::make_unique<WriterDatabase>(cfg_);
        db_->Open();
        db_->CreateInternalTables();
    }

    void TearDown() override { db_.reset(); }

    test::TempDirectory directory_;
    Config cfg_;
    std::unique_ptr<WriterDatabase> db_;
};

TEST_F(MigrationManagerTest, AppliesOneMigrationAtATimeInVersionOrderAndStopsWhenExhausted) {
    std::ranges::reverse(cfg_.migrations);
    MigrationManager mgr{*db_, cfg_.migrations};
    std::vector<int> expected;
    for (int version : {1, 2, 5}) {
        SCOPED_TRACE(version);
        ASSERT_TRUE(mgr.ApplyPendingMigrations());
        expected.push_back(version);
        EXPECT_EQ(db_->GetAppliedVersions(), expected);
    }
    EXPECT_FALSE(mgr.ApplyPendingMigrations());  // no more pending
    EXPECT_FALSE(mgr.ApplyPendingMigrations());
    EXPECT_EQ(db_->GetAppliedVersions(), expected);
}

TEST_F(MigrationManagerTest, StartVersionSkipsEarlierPendingMigrations) {
    MigrationManager mgr{*db_, cfg_.migrations};
    ASSERT_TRUE(mgr.ApplyPendingMigrations(0));  // Apply v1 first so the table exists.
    ASSERT_TRUE(mgr.ApplyPendingMigrations(2));  // Skip pending v2 and choose v5.
    EXPECT_EQ(db_->GetAppliedVersions(), (std::vector<int>{1, 5}));
    EXPECT_FALSE(mgr.ApplyPendingMigrations(5));
    ASSERT_TRUE(mgr.ApplyPendingMigrations());
    EXPECT_EQ(db_->GetAppliedVersions(), (std::vector<int>{1, 2, 5}));
}

TEST_F(MigrationManagerTest, ForcedRollbackRemovesSchemaAndVersionAndAllowsReapplication) {
    MigrationManager mgr{*db_, cfg_.migrations};
    ASSERT_TRUE(mgr.ApplyPendingMigrations());
    EXPECT_EQ(db_->GetAppliedVersions(), (std::vector<int>{1}));
    // Rollback with force=true should not prompt.
    ASSERT_TRUE(mgr.RollbackMigration(1, true));
    EXPECT_TRUE(db_->GetAppliedVersions().empty());
    EXPECT_TRUE(db_->FetchTableColumns("TestLog").empty());
    ASSERT_TRUE(mgr.ApplyPendingMigrations());
    EXPECT_EQ(db_->GetAppliedVersions(), (std::vector<int>{1}));
    EXPECT_FALSE(db_->FetchTableColumns("TestLog").empty());
}

TEST_F(MigrationManagerTest, RollbackUnknownVersionThrows) {
    MigrationManager mgr{*db_, cfg_.migrations};

    EXPECT_THROW(mgr.RollbackMigration(999, true), std::runtime_error);
}

TEST_F(MigrationManagerTest, FailedRolloutThrowsAndRollsBackSchemaChanges) {
    cfg_.migrations[0].rollout.push_back("INVALID SQL");
    MigrationManager mgr{*db_, cfg_.migrations};
    EXPECT_THROW(mgr.ApplyPendingMigrations(), std::runtime_error);
    EXPECT_TRUE(db_->GetAppliedVersions().empty());
    EXPECT_TRUE(db_->FetchTableColumns("TestLog").empty());
}

TEST_F(MigrationManagerTest, FailedRollbackThrowsAndKeepsAppliedVersion) {
    cfg_.migrations[0].rollback.push_back("INVALID SQL");
    MigrationManager mgr{*db_, cfg_.migrations};
    ASSERT_TRUE(mgr.ApplyPendingMigrations());
    EXPECT_THROW(mgr.RollbackMigration(1, true), std::runtime_error);
    EXPECT_TRUE(range_contains(db_->GetAppliedVersions(), 1));
    EXPECT_FALSE(db_->FetchTableColumns("TestLog").empty());
}

TEST_F(MigrationManagerTest, FailedVersionInsertThrowsAndRollsBackSchemaChanges) {
    ASSERT_TRUE(db_->ApplyMigration(
        0, {"CREATE TRIGGER reject_version BEFORE INSERT ON versions WHEN NEW.version = 1 "
            "BEGIN SELECT RAISE(ABORT, 'cannot record migration'); END"}));
    MigrationManager mgr{*db_, cfg_.migrations};
    EXPECT_THROW(mgr.ApplyPendingMigrations(), std::runtime_error);
    EXPECT_FALSE(range_contains(db_->GetAppliedVersions(), 1));
    EXPECT_TRUE(db_->FetchTableColumns("TestLog").empty());
}

TEST_F(MigrationManagerTest, InitializeAppliesEveryPendingMigrationAndRefreshesSchema) {
    // Three sequential migrations: base table + two ALTER TABLE additions.
    cfg_.auto_rollout = true;
    db_->Initialize();  // must apply v1, v2, v5 — not just v1
    EXPECT_EQ(db_->GetAppliedVersions(), (std::vector<int>{1, 2, 5}));
    const auto columns = db_->GetColumnInfo();
    for (const auto* name : {"timestamp", "message", "level", "service"}) {
        SCOPED_TRACE(name);
        EXPECT_NE(std::ranges::find(columns, name, &ColumnInfo::name), columns.end());
    }
    db_->Initialize();
    EXPECT_EQ(db_->GetAppliedVersions(), (std::vector<int>{1, 2, 5}));
}
