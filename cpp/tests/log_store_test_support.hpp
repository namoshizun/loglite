#ifndef LOGLITE_LOG_STORE_TEST_SUPPORT_HPP_
#define LOGLITE_LOG_STORE_TEST_SUPPORT_HPP_

#include "log_reader.hpp"
#include "log_store.hpp"
#include "partition.hpp"
#include "test_support.hpp"

namespace loglite::test {

inline void ExecuteFile(const std::filesystem::path& path, std::string_view sql) {
    sqlite3* raw{};
    const int result = sqlite3_open(path.c_str(), &raw);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection{raw, sqlite3_close};
    if (result != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(raw));
    Statement{raw, sql}.Step();
}

inline int64_t ScalarFile(const std::filesystem::path& path, std::string_view sql) {
    sqlite3* raw{};
    const int result = sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection{raw, sqlite3_close};
    if (result != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(raw));
    Statement statement{raw, sql};
    return statement.Step() == SQLITE_ROW ? sqlite3_column_int64(statement, 0) : 0;
}

class LogStoreFixture : public ::testing::Test {
   protected:
    void SetUp() override { OpenStore(); }

    void OpenStore() {
        pool_.reset();
        db_ = std::make_unique<LogStore>(cfg_);
        db_->Open();
        db_->Initialize();
        pool_ = std::make_unique<LogReaderPool>(*db_, 1);
    }

    PaginatedQueryResult Query(const std::vector<std::string>& fields,
                               const std::vector<QueryFilter>& filters, int limit, int offset) {
        return pool_->UseConnection(
            [&](LogReader& reader) { return reader.Query(fields, filters, limit, offset); });
    }

    std::vector<std::filesystem::path> PartitionFiles() const {
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory_.path())) {
            if (PartitionScheme::IntervalOf(entry.path().filename().string()))
                paths.push_back(entry.path());
        }
        std::ranges::sort(paths);
        return paths;
    }

    TempDirectory directory_;
    Config cfg_{MakeConfig(directory_.path())};
    std::unique_ptr<LogStore> db_;
    std::unique_ptr<LogReaderPool> pool_;
};

}  // namespace loglite::test

#endif  // LOGLITE_LOG_STORE_TEST_SUPPORT_HPP_
