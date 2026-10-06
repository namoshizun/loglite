#ifndef LOGLITE_READER_DATABASE_HPP_
#define LOGLITE_READER_DATABASE_HPP_

#include "database.hpp"

#include <boost/asio.hpp>

namespace asio = boost::asio;

#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace loglite {

class ReaderDatabase final : public Database {
   public:
    enum class LogOrder { kNewestFirst, kIdAscending };

    struct LogRow {
        int64_t id{};
        nlohmann::json values;
    };

    ReaderDatabase(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog);

    void Open();
    void Open(const std::filesystem::path& path);

    // A short-lived connection to another file with this schema, decoding through
    // that file's own column dictionary.
    [[nodiscard]] std::unique_ptr<ReaderDatabase> OpenFile(const std::filesystem::path& path) const;

    PaginatedQueryResult Query(const std::vector<std::string>& fields,
                               const std::vector<QueryFilter>& filters, int limit,
                               int offset) const;

    // Expands "*" and validates the projection.
    [[nodiscard]] std::vector<std::string> ResolveFields(
        const std::vector<std::string>& fields) const;
    [[nodiscard]] int64_t CountLogs(const std::vector<QueryFilter>& filters) const;
    // `fields` must be resolved. Negative limits follow SQLite: no limit.
    [[nodiscard]] std::vector<LogRow> ReadLogs(const std::vector<std::string>& fields,
                                               const std::vector<QueryFilter>& filters,
                                               LogOrder order, int limit, int offset) const;

    StatsQueryResult QueryActivityStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const;
    StatsQueryResult QueryDatabaseStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const;

    bool Ping() const;

   private:
    nlohmann::json DecodeRow(sqlite3_stmt* stmt, const std::vector<std::string>& fields) const;
    void LoadReadDictionary();
};

class ReadDatabasePool {
   public:
    ReadDatabasePool(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog, size_t size);
    ~ReadDatabasePool();

    ReadDatabasePool(const ReadDatabasePool&) = delete;
    ReadDatabasePool& operator=(const ReadDatabasePool&) = delete;

    template <std::invocable<ReaderDatabase&> F>
    auto UseConnection(F&& f) -> std::invoke_result_t<F, ReaderDatabase&> {
        ConnectionLease lease{*this};
        return std::invoke(std::forward<F>(f), lease.db());
    }

    template <std::invocable<ReaderDatabase&> F>
    asio::awaitable<std::invoke_result_t<F, ReaderDatabase&>> AsyncUseConnection(
        asio::any_io_executor reader_ex, F&& f) {
        using Result = std::invoke_result_t<F, ReaderDatabase&>;
        return asio::co_spawn(
            std::move(reader_ex),
            [this, f = std::forward<F>(f)]() mutable -> asio::awaitable<Result> {
                co_return UseConnection(std::move(f));
            },
            asio::use_awaitable);
    }

    void Close();

   private:
    class ConnectionLease {
       public:
        explicit ConnectionLease(ReadDatabasePool& pool) : pool_(&pool), db_(&pool.acquire()) {}
        ~ConnectionLease() { pool_->release(*db_); }

        ConnectionLease(const ConnectionLease&) = delete;
        ConnectionLease& operator=(const ConnectionLease&) = delete;

        ReaderDatabase& db() const noexcept { return *db_; }

       private:
        ReadDatabasePool* pool_;
        ReaderDatabase* db_;
    };

    ReaderDatabase& acquire();
    void release(ReaderDatabase& db);

    std::vector<std::unique_ptr<ReaderDatabase>> readers_;
    std::queue<ReaderDatabase*> available_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool closed_{false};
};

}  // namespace loglite

#endif  // LOGLITE_READER_DATABASE_HPP_
