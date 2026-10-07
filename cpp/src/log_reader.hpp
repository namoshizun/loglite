#ifndef LOGLITE_LOG_READER_HPP_
#define LOGLITE_LOG_READER_HPP_

#include "log_store.hpp"
#include "query_plan.hpp"
#include "reader_pool.hpp"

namespace loglite {

// A logical read view borrowed for the lifetime of a physical connection lease.
// The store owns the file layout; ReaderDatabase owns single-file SQL operations.
class LogReader {
   public:
    LogReader(const LogStore& store, ReaderDatabase& connection)
        : store_(store), connection_(connection) {}

    PaginatedQueryResult Query(const std::vector<std::string>& fields,
                               const std::vector<QueryFilter>& filters, int limit,
                               int offset) const;
    PaginatedQueryResult Query(const QueryPlan& plan) const;
    LogIdQueryResult QueryLogIdRange(const std::vector<std::string>& fields,
                                     int64_t since_exclusive, int64_t until_inclusive,
                                     int limit) const;

    StatsQueryResult QueryActivityStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const {
        return connection_.QueryActivityStats(since, until, fields, ordering);
    }
    StatsQueryResult QueryDatabaseStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const {
        return connection_.QueryDatabaseStats(since, until, fields, ordering);
    }
    bool Ping() const { return connection_.Ping(); }

   private:
    // The pooled connection serves logs.db; each partition file gets a short-lived
    // connection, so a query never holds many files open.
    template <typename F>
    void UseFile(const std::filesystem::path& path, F&& use) const {
        if (path == store_.config().db_path) return use(connection_);
        const auto file = connection_.OpenFile(path);
        use(*file);
    }

    const LogStore& store_;
    ReaderDatabase& connection_;
};

// Reuse the physical pool's scheduling and connection leases. Each callback gets
// a short-lived logical view, so there is only one pool and one queue to manage.
class LogReaderPool {
   public:
    LogReaderPool(const LogStore& store, size_t size)
        : store_(store), connections_(store.config(), store.catalog(), size) {}

    template <std::invocable<LogReader&> F>
    auto UseConnection(F&& f) -> std::invoke_result_t<F, LogReader&> {
        return connections_.UseConnection(
            [&](ReaderDatabase& connection) -> std::invoke_result_t<F, LogReader&> {
                LogReader reader{store_, connection};
                return std::invoke(std::forward<F>(f), reader);
            });
    }

    template <std::invocable<LogReader&> F>
    asio::awaitable<std::invoke_result_t<F, LogReader&>> AsyncUseConnection(
        asio::any_io_executor reader_ex, F&& f) {
        using Result = std::invoke_result_t<F, LogReader&>;
        return connections_.AsyncUseConnection(
            std::move(reader_ex),
            [this, f = std::forward<F>(f)](ReaderDatabase& connection) mutable -> Result {
                LogReader reader{store_, connection};
                return std::invoke(std::move(f), reader);
            });
    }

    void Close() { connections_.Close(); }

   private:
    const LogStore& store_;
    ReadDatabasePool connections_;
};

}  // namespace loglite

#endif  // LOGLITE_LOG_READER_HPP_
