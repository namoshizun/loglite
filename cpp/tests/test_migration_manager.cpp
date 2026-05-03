#include <gtest/gtest.h>

#include "config.hpp"
#include "database.hpp"
#include "migrations.hpp"

#include <filesystem>

namespace fs = std::filesystem;
using namespace loglite;

class MigrationManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_ = fs::temp_directory_path() / "loglite_mgr_test";
        fs::remove_all(tmp_);
        fs::create_directories(tmp_);

        cfg_.sqlite_dir = tmp_;
        cfg_.db_path = tmp_ / "logs.db";
        cfg_.log_table_name = "TestLog";
        cfg_.log_timestamp_field = "timestamp";
        cfg_.auto_rollout = false;
        cfg_.compression = {false, {}};

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

        db_ = std::make_unique<Database>(cfg_);
        db_->Open();
        db_->CreateInternalTables();
    }

    void TearDown() override {
        db_.reset();
        fs::remove_all(tmp_);
    }

    fs::path tmp_;
    Config cfg_;
    std::unique_ptr<Database> db_;
};

TEST_F(MigrationManagerTest, ApplyPendingMigrationsAppliesFirstUnapplied) {
    MigrationManager mgr{*db_, cfg_.migrations};

    bool ok = mgr.ApplyPendingMigrations();
    EXPECT_TRUE(ok);

    auto versions = db_->GetAppliedVersions();
    EXPECT_TRUE(std::ranges::contains(versions, 1));
    EXPECT_FALSE(std::ranges::contains(versions, 2));
}

TEST_F(MigrationManagerTest, ApplyPendingMigrationsSequentially) {
    MigrationManager mgr{*db_, cfg_.migrations};

    EXPECT_TRUE(mgr.ApplyPendingMigrations());
    EXPECT_TRUE(mgr.ApplyPendingMigrations());
    EXPECT_TRUE(mgr.ApplyPendingMigrations());
    EXPECT_FALSE(mgr.ApplyPendingMigrations());  // no more pending

    auto versions = db_->GetAppliedVersions();
    EXPECT_TRUE(std::ranges::contains(versions, 1));
    EXPECT_TRUE(std::ranges::contains(versions, 2));
    EXPECT_TRUE(std::ranges::contains(versions, 5));
}

TEST_F(MigrationManagerTest, ApplyPendingMigrationsRespectsStartVersion) {
    MigrationManager mgr{*db_, cfg_.migrations};

    // Apply v1 first so the table exists
    EXPECT_TRUE(mgr.ApplyPendingMigrations(0));
    auto v1 = db_->GetAppliedVersions();
    EXPECT_TRUE(std::ranges::contains(v1, 1));

    // With start_version=1, the next migration (v2) should be picked
    EXPECT_TRUE(mgr.ApplyPendingMigrations(1));

    auto versions = db_->GetAppliedVersions();
    EXPECT_TRUE(std::ranges::contains(versions, 1));
    EXPECT_TRUE(std::ranges::contains(versions, 2));
    EXPECT_FALSE(std::ranges::contains(versions, 5));
}

TEST_F(MigrationManagerTest, ApplyPendingMigrationsWithNegativeStart) {
    MigrationManager mgr{*db_, cfg_.migrations};

    EXPECT_TRUE(mgr.ApplyPendingMigrations(-10));

    auto versions = db_->GetAppliedVersions();
    EXPECT_TRUE(std::ranges::contains(versions, 1));
}

TEST_F(MigrationManagerTest, RollbackMigrationWorks) {
    MigrationManager mgr{*db_, cfg_.migrations};

    // Apply v1 first
    mgr.ApplyPendingMigrations();
    EXPECT_TRUE(std::ranges::contains(db_->GetAppliedVersions(), 1));

    // Rollback with force=true
    EXPECT_TRUE(mgr.RollbackMigration(1, true));

    auto versions = db_->GetAppliedVersions();
    EXPECT_FALSE(std::ranges::contains(versions, 1));
}

TEST_F(MigrationManagerTest, RollbackUnknownVersionThrows) {
    MigrationManager mgr{*db_, cfg_.migrations};

    EXPECT_THROW(mgr.RollbackMigration(999, true), std::runtime_error);
}

TEST_F(MigrationManagerTest, RollbackWithForceTrueSkipsPrompt) {
    MigrationManager mgr{*db_, cfg_.migrations};

    // Apply v1 first
    mgr.ApplyPendingMigrations();
    EXPECT_TRUE(std::ranges::contains(db_->GetAppliedVersions(), 1));

    // Rollback with force=true should not prompt
    EXPECT_TRUE(mgr.RollbackMigration(1, true));

    auto versions = db_->GetAppliedVersions();
    EXPECT_FALSE(std::ranges::contains(versions, 1));
}

TEST_F(MigrationManagerTest, RollbackNotFoundInConfig) {
    MigrationManager mgr{*db_, cfg_.migrations};

    mgr.ApplyPendingMigrations();  // apply v1
    EXPECT_THROW(mgr.RollbackMigration(10, true), std::runtime_error);
}
