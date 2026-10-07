#ifndef LOGLITE_ACCESS_HPP_
#define LOGLITE_ACCESS_HPP_

#include "ingestion.hpp"
#include "notifier.hpp"
#include "query_service.hpp"
#include "schema.hpp"

#include <atomic>
#include <chrono>

namespace loglite {

// What HTTP is allowed to see: ingestion, queries, the live feed, settings
// and status. Not the backlog, not file-local connections.
struct HttpAccess {
    const Config& config;
    Ingestion& ingestion;
    QueryService& queries;
    LogNotifier& live;
    const LogSchema& schema;
    const std::atomic<bool>& stopping;
    std::chrono::steady_clock::time_point started_at;

    [[nodiscard]] bool StopRequested() const noexcept {
        return stopping.load(std::memory_order_acquire);
    }
};

}  // namespace loglite

#endif  // LOGLITE_ACCESS_HPP_
