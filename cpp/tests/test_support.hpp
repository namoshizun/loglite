#ifndef LOGLITE_TESTS_TEST_SUPPORT_HPP_
#define LOGLITE_TESTS_TEST_SUPPORT_HPP_

#include "config.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <system_error>
#include <thread>

namespace loglite::test {

// Each test owns its directory, including when several test processes run at once.
class TempDirectory {
   public:
    TempDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "loglite-test-XXXXXX").string();
        const auto* path = ::mkdtemp(pattern.data());
        if (!path) throw std::system_error(errno, std::generic_category(), "mkdtemp");
        path_ = path;
    }
    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

   private:
    std::filesystem::path path_;
};

inline Config MakeConfig(const std::filesystem::path& directory) {
    Config cfg;
    cfg.sqlite_dir = directory;
    cfg.db_path = directory / "logs.db";
    cfg.log_table_name = "TestLog";
    cfg.auto_rollout = true;  // let Initialize() apply migrations
    cfg.compression = {false, {}};
    cfg.migrations = {{1,
                       {"CREATE TABLE TestLog (id INTEGER PRIMARY KEY, timestamp TEXT NOT NULL, "
                        "message TEXT NOT NULL, level TEXT NOT NULL, service TEXT)"},
                       {"DROP TABLE TestLog"}}};
    return cfg;
}

class DatabaseFixture : public ::testing::Test {
   protected:
    void SetUp() override { OpenDatabase(); }
    void TearDown() override {
        reader_.reset();
        db_.reset();
    }

    void OpenDatabase() {
        reader_.reset();
        db_ = std::make_unique<WriterDatabase>(cfg_);
        db_->Open();
        db_->Initialize();
        reader_ = std::make_unique<ReaderDatabase>(cfg_, db_->catalog());
        reader_->Open();
    }

    TempDirectory directory_;
    Config cfg_{MakeConfig(directory_.path())};
    std::unique_ptr<WriterDatabase> db_;
    std::unique_ptr<ReaderDatabase> reader_;
};

template <typename F>
bool WaitUntil(F&& ready, std::chrono::milliseconds timeout = std::chrono::seconds{3}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (std::invoke(ready)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

}  // namespace loglite::test

#endif  // LOGLITE_TESTS_TEST_SUPPORT_HPP_
