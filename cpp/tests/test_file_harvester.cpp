#include <gtest/gtest.h>

#include "backlog.hpp"
#include "harvesters/file.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace loglite;
using namespace loglite::harvesters;
using namespace std::literals::chrono_literals;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Poll until `backlog` holds at least `n` entries, or `timeout` elapses.
static bool wait_for(Backlog& bl, size_t n,
                     std::chrono::milliseconds timeout = 2500ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (bl.Size() >= n) return true;
        std::this_thread::sleep_for(50ms);
    }
    return false;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class FileHarvesterTest : public ::testing::Test {
   protected:
    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / "loglite_fh_test";
        fs::remove_all(tmp_dir_);
        fs::create_directories(tmp_dir_);
        log_file_ = tmp_dir_ / "app.log";
    }

    void TearDown() override {
        if (harvester_) {
            harvester_->Stop();
            harvester_.reset();
        }
        fs::remove_all(tmp_dir_);
    }

    // Append `line` (+ newline) to the tailed file, creating it if absent.
    void append(const std::string& line) {
        std::ofstream f{log_file_, std::ios::app};
        f << line << "\n";
    }

    // Create an empty file at log_file_ (so the harvester doesn't enter the
    // 5-second "waiting for file" loop) then start the harvester.
    void create_file_and_start() {
        { std::ofstream{log_file_}; }  // empty file
        harvester_ = std::make_unique<FileHarvester>("test", log_file_, backlog_);
        harvester_->Start();
        // Give the harvester time to open the file and record the initial EOF offset.
        std::this_thread::sleep_for(300ms);
    }

    fs::path tmp_dir_;
    fs::path log_file_;
    Backlog backlog_{1000};
    std::unique_ptr<FileHarvester> harvester_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(FileHarvesterTest, IngestsNewLinesAppendedAfterStart) {
    // Pre-populate the file so the harvester seeks to a non-zero EOF.
    append(R"({"msg":"pre-existing","level":"INFO"})");

    harvester_ = std::make_unique<FileHarvester>("test", log_file_, backlog_);
    harvester_->Start();
    std::this_thread::sleep_for(300ms);

    // Only this line — written after Start() — should be ingested.
    append(R"({"msg":"new-entry","level":"ERROR"})");

    ASSERT_TRUE(wait_for(backlog_, 1)) << "harvester did not ingest the new line in time";

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "new-entry");
}

TEST_F(FileHarvesterTest, SkipsPreExistingContent) {
    // tail -F semantics: content written before Start() must be ignored.
    append(R"({"msg":"old","level":"INFO"})");
    append(R"({"msg":"also-old","level":"DEBUG"})");

    harvester_ = std::make_unique<FileHarvester>("test", log_file_, backlog_);
    harvester_->Start();
    std::this_thread::sleep_for(800ms);

    EXPECT_EQ(backlog_.Size(), 0u) << "pre-existing content should not have been ingested";
}

TEST_F(FileHarvesterTest, SkipsNonJsonLines) {
    // Pre-populate and start so the harvester is past the existing content.
    append(R"({"msg":"pre-existing"})");
    harvester_ = std::make_unique<FileHarvester>("test", log_file_, backlog_);
    harvester_->Start();
    std::this_thread::sleep_for(300ms);

    // Mix of bad and good lines written after Start().
    append("not json at all");
    append("{ broken json :");
    append(R"({"msg":"valid","level":"DEBUG"})");

    ASSERT_TRUE(wait_for(backlog_, 1));

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u) << "only the valid JSON line should have been ingested";
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "valid");
}

TEST_F(FileHarvesterTest, AddsTimestampWhenMissing) {
    create_file_and_start();

    append(R"({"msg":"no-ts","level":"INFO"})");

    ASSERT_TRUE(wait_for(backlog_, 1));

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_TRUE(entries[0].contains("timestamp")) << "timestamp field should have been injected";
    EXPECT_FALSE(entries[0]["timestamp"].get<std::string>().empty());
}

TEST_F(FileHarvesterTest, PreservesExistingTimestamp) {
    create_file_and_start();

    append(R"({"msg":"with-ts","level":"INFO","timestamp":"2024-01-01T00:00:00Z"})");

    ASSERT_TRUE(wait_for(backlog_, 1));

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["timestamp"].get<std::string>(), "2024-01-01T00:00:00Z");
}

TEST_F(FileHarvesterTest, IngestsMultipleLinesInOnePoll) {
    create_file_and_start();

    // Write several lines at once so they land in a single poll iteration.
    {
        std::ofstream f{log_file_, std::ios::app};
        for (int i = 0; i < 5; ++i)
            f << R"({"msg":"batch","id":)" << i << "}\n";
    }

    ASSERT_TRUE(wait_for(backlog_, 5));
    EXPECT_EQ(backlog_.Flush().size(), 5u);
}

TEST_F(FileHarvesterTest, DetectsTruncation) {
    create_file_and_start();

    // Write and ingest a first batch.
    append(R"({"msg":"before-truncate","level":"INFO"})");
    ASSERT_TRUE(wait_for(backlog_, 1)) << "first batch not ingested";
    backlog_.Flush();

    // Truncate the file (simulates logrotate copytruncate).
    { std::ofstream{log_file_, std::ios::trunc}; }

    // Give the harvester at least one poll to observe the size drop.
    std::this_thread::sleep_for(600ms);

    // Write new content into the truncated (now-empty) file.
    append(R"({"msg":"after-truncate","level":"WARN"})");

    ASSERT_TRUE(wait_for(backlog_, 1)) << "harvester did not recover after file truncation";

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "after-truncate");
}

TEST_F(FileHarvesterTest, DetectsRotation) {
    create_file_and_start();

    // Ingest something so the harvester records a non-zero offset.
    append(R"({"msg":"pre-rotate","level":"INFO"})");
    ASSERT_TRUE(wait_for(backlog_, 1)) << "pre-rotation entry not ingested";
    backlog_.Flush();

    // Simulate rotation: rename the current file, then create a fresh one.
    fs::rename(log_file_, tmp_dir_ / "app.log.1");

    // Write to the new file at the original path (append() creates it).
    append(R"({"msg":"post-rotate","level":"INFO"})");

    ASSERT_TRUE(wait_for(backlog_, 1, 3s))
        << "harvester did not detect rotation and pick up the new file";

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "post-rotate");
}
