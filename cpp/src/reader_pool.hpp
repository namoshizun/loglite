#ifndef LOGLITE_READER_POOL_HPP_
#define LOGLITE_READER_POOL_HPP_

#include "reader_database.hpp"

#include <boost/asio.hpp>

#include <concepts>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

namespace asio = boost::asio;

namespace loglite {

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

#endif  // LOGLITE_READER_POOL_HPP_
