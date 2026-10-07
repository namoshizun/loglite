#include <gtest/gtest.h>

#include "test_support.hpp"
#include "utils.hpp"
#include "backlog.hpp"
#include "harvesters/file.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace loglite;
using namespace loglite::harvesters;
using namespace std::literals::chrono_literals;

// ── Fixture ───────────────────────────────────────────────────────────────────

class FileHarvesterTest : public ::testing::Test {
   protected:
    void SetUp() override {
        tmp_dir_ = directory_.path();
        log_file_ = tmp_dir_ / "app.log";
    }

    void TearDown() override {
        if (harvester_) {
            harvester_->Stop();
            harvester_.reset();
        }
    }

    // Append `line` (+ newline) to the tailed file, creating it if absent.
    void append(const std::string& line) {
        std::ofstream f{log_file_, std::ios::app};
        f << line << "\n";
    }

    void start() {
        harvester_ = std::make_unique<FileHarvester>("test", log_file_, backlog_, options_);
        harvester_->Start();
    }

    void create_file_and_start() {
        std::ofstream{log_file_};
        start();
    }

    test::TempDirectory directory_;
    fs::path tmp_dir_;
    fs::path log_file_;
    Backlog backlog_{1000};
    FileHarvester::Options options_{
        .poll_interval = 20ms, .missing_file_interval = 20ms, .rotation_drain_grace = 50ms};
    std::unique_ptr<FileHarvester> harvester_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(FileHarvesterTest, IngestsNewLinesAppendedAfterStart) {
    // Pre-populate the file so the harvester seeks to a non-zero EOF.
    append(R"({"msg":"pre-existing","level":"INFO"})");

    start();
    ASSERT_EQ(harvester_->ReadOffset(), fs::file_size(log_file_));

    // Only this line — written after Start() — should be ingested.
    append(R"({"msg":"new-entry","level":"ERROR"})");

    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 1; }))
        << "harvester did not ingest the new line in time";

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "new-entry");
}

TEST_F(FileHarvesterTest, SkipsNonJsonLines) {
    // Pre-populate and start so the harvester is past the existing content.
    append(R"({"msg":"pre-existing"})");
    start();

    // Mix of bad and good lines written after Start().
    append("not json at all");
    append("{ broken json :");
    append(R"({"msg":"valid","level":"DEBUG"})");

    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 1; }));

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u) << "only the valid JSON line should have been ingested";
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "valid");
}

TEST_F(FileHarvesterTest, BatchIngestionAddsMissingTimestampsAndPreservesExistingOnes) {
    create_file_and_start();
    // Write several lines at once so they land in a single poll iteration.
    append(R"({"msg":"no-ts","level":"INFO"})");
    append(R"({"msg":"with-ts","level":"INFO","timestamp":"2024-01-01T00:00:00Z"})");
    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 2; }));
    const auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0]["msg"], "no-ts");
    ASSERT_TRUE(entries[0].contains("timestamp"));
    const auto timestamp = entries[0]["timestamp"].get<std::string>();
    EXPECT_TRUE(parse_iso8601(timestamp).has_value());
    EXPECT_NE(timestamp.find('.'), std::string::npos)
        << "injected timestamp should include milliseconds";
    EXPECT_EQ(entries[1]["msg"], "with-ts");
    EXPECT_EQ(entries[1]["timestamp"], "2024-01-01T00:00:00Z");
}

TEST_F(FileHarvesterTest, DetectsTruncation) {
    create_file_and_start();

    // Write and ingest a first batch.
    append(R"({"msg":"before-truncate","level":"INFO"})");
    ASSERT_TRUE(test::WaitUntil([&] {
        return harvester_->ReadOffset() == fs::file_size(log_file_);
    })) << "first batch not read";
    ASSERT_EQ(backlog_.Size(), 1u);
    backlog_.Flush();

    // Truncate the file (simulates logrotate copytruncate).
    {
        std::ofstream{log_file_, std::ios::trunc};
    }

    ASSERT_TRUE(test::WaitUntil([&] { return harvester_->ReadOffset() == 0; }))
        << "harvester did not observe truncation";

    // Write new content into the truncated (now-empty) file.
    append(R"({"msg":"after-truncate","level":"WARN"})");

    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 1; }))
        << "harvester did not recover after file truncation";

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "after-truncate");
}

TEST_F(FileHarvesterTest, DetectsRotation) {
    create_file_and_start();

    // Ingest something so the harvester records a non-zero offset.
    append(R"({"msg":"pre-rotate","level":"INFO"})");
    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 1; }))
        << "pre-rotation entry not ingested";
    backlog_.Flush();

    // Simulate rotation: rename the current file, then create a fresh one.
    fs::rename(log_file_, tmp_dir_ / "app.log.1");

    // Write to the new file at the original path (append() creates it).
    append(R"({"msg":"post-rotate","level":"INFO"})");

    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 1; }))
        << "harvester did not detect rotation and pick up the new file";

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "post-rotate");
}

TEST_F(FileHarvesterTest, IngestsPartialWritesAcrossPolls) {
    create_file_and_start();

    // 1. Write first part of the JSON log without a newline.
    {
        std::ofstream f{log_file_, std::ios::app};
        f << R"({"msg":"partial-write","level":"INF)";
    }

    ASSERT_TRUE(test::WaitUntil([&] {
        return harvester_->ReadOffset() == fs::file_size(log_file_);
    })) << "harvester did not buffer the partial write";

    // Verify it wasn't ingested yet (incomplete JSON, no newline).
    EXPECT_EQ(backlog_.Size(), 0u);

    // 2. Append the rest of the line with a newline.
    {
        std::ofstream f{log_file_, std::ios::app};
        f << R"(O"})" << "\n";
    }

    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 1; }))
        << "Harvester failed to assemble partial write";

    auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["msg"].get<std::string>(), "partial-write");
    EXPECT_EQ(entries[0]["level"].get<std::string>(), "INFO");
}

TEST_F(FileHarvesterTest, StopDrainsUnreadBytesAndFlushesPartialLine) {
    options_.poll_interval = 10s;
    options_.start_at_end = false;
    append(R"({"msg":"first"})");
    start();
    ASSERT_TRUE(
        test::WaitUntil([&] { return harvester_->ReadOffset() == fs::file_size(log_file_); }));

    {
        std::ofstream out{log_file_, std::ios::app};
        out << R"({"msg":"second"})" << '\n' << R"({"msg":"partial"})";
    }
    harvester_->Stop();
    EXPECT_FALSE(harvester_->ReadOffset().has_value());

    const auto entries = backlog_.Flush();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0]["msg"], "first");
    EXPECT_EQ(entries[1]["msg"], "second");
    EXPECT_EQ(entries[2]["msg"], "partial");
}

TEST_F(FileHarvesterTest, RetriesOpeningMissingFile) {
    options_.start_at_end = false;
    start();
    ASSERT_FALSE(harvester_->ReadOffset().has_value());
    append(R"({"msg":"created-later"})");

    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 1; }));
    EXPECT_EQ(backlog_.Flush().front()["msg"], "created-later");
}

TEST_F(FileHarvesterTest, RestartCapturesNewInitialEof) {
    create_file_and_start();
    harvester_->Stop();
    append(R"({"msg":"while-stopped"})");
    harvester_->Start();
    append(R"({"msg":"after-restart"})");

    ASSERT_TRUE(test::WaitUntil([&] { return backlog_.Size() == 1; }));
    EXPECT_EQ(backlog_.Flush().front()["msg"], "after-restart");
}
