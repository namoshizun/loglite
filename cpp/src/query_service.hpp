#ifndef LOGLITE_QUERY_SERVICE_HPP_
#define LOGLITE_QUERY_SERVICE_HPP_

#include "log_reader.hpp"
#include "query_plan.hpp"
#include "schedule.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace loglite {

// Transport submits a logical request. This service validates it and reads
// through the reader pool. Scheduling stays here, not in the file databases.
class QueryService {
   public:
    QueryService(LogReaderPool& readers, const LogStore& store, asio::any_io_executor executor)
        : readers_(readers), store_(store), executor_(std::move(executor)) {}

    [[nodiscard]] asio::awaitable<PaginatedQueryResult> Execute(QueryRequest request) const {
        co_return co_await Schedule(executor_, [this, request = std::move(request)] {
            const auto plan = PrepareQuery(store_.schema(), request);
            return readers_.UseConnection([&](LogReader& reader) { return reader.Query(plan); });
        });
    }

    [[nodiscard]] asio::awaitable<StatsQueryResult> Activity(std::string since, std::string until,
                                                             std::vector<std::string> fields,
                                                             std::string ordering) const {
        co_return co_await Schedule(
            executor_, [this, since = std::move(since), until = std::move(until),
                        fields = std::move(fields), ordering = std::move(ordering)] {
                return readers_.UseConnection([&](LogReader& reader) {
                    return reader.QueryActivityStats(since, until, fields, ordering);
                });
            });
    }

    [[nodiscard]] asio::awaitable<StatsQueryResult> DatabaseStats(std::string since,
                                                                  std::string until,
                                                                  std::vector<std::string> fields,
                                                                  std::string ordering) const {
        co_return co_await Schedule(
            executor_, [this, since = std::move(since), until = std::move(until),
                        fields = std::move(fields), ordering = std::move(ordering)] {
                return readers_.UseConnection([&](LogReader& reader) {
                    return reader.QueryDatabaseStats(since, until, fields, ordering);
                });
            });
    }

    [[nodiscard]] asio::awaitable<bool> Ping() const {
        co_return co_await Schedule(executor_, [this] {
            return readers_.UseConnection([](LogReader& reader) { return reader.Ping(); });
        });
    }

    [[nodiscard]] int64_t EstimateRows() const { return store_.EstimateLogRowCount(); }

   private:
    LogReaderPool& readers_;
    const LogStore& store_;
    asio::any_io_executor executor_;
};

}  // namespace loglite

#endif  // LOGLITE_QUERY_SERVICE_HPP_
