#ifndef LOGLITE_CONTEXT_HPP_
#define LOGLITE_CONTEXT_HPP_

#include "backlog.hpp"
#include "config.hpp"
#include "notifier.hpp"
#include "log_reader.hpp"
#include "log_store.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <ranges>
#include <vector>

#include <boost/asio.hpp>

namespace asio = boost::asio;

namespace loglite {

// Aggregates all shared mutable state passed to handlers and background tasks.
// Passed by reference; must outlive all coroutines.

struct ServerContext {
    Config& config;
    LogStore& db_write;
    LogReaderPool& db_read;
    Backlog& backlog;
    LogNotifier& notifier;

    asio::strand<asio::thread_pool::executor_type> write_strand;
    asio::thread_pool::executor_type reader_executor;
    std::chrono::steady_clock::time_point server_started_at;

    std::atomic<bool> stopping{false};
    std::vector<std::weak_ptr<asio::steady_timer>> shutdown_timers;

    ServerContext(Config& config_in, LogStore& db_write_in, LogReaderPool& db_read_in,
                  Backlog& backlog_in, LogNotifier& notifier_in,
                  asio::strand<asio::thread_pool::executor_type> write_strand_in,
                  asio::thread_pool::executor_type reader_executor_in,
                  std::chrono::steady_clock::time_point server_started_at_in =
                      std::chrono::steady_clock::now())
        : config(config_in),
          db_write(db_write_in),
          db_read(db_read_in),
          backlog(backlog_in),
          notifier(notifier_in),
          write_strand(std::move(write_strand_in)),
          reader_executor(std::move(reader_executor_in)),
          server_started_at(server_started_at_in) {}

    void RegisterShutdownTimer(const std::shared_ptr<asio::steady_timer>& timer) {
        std::erase_if(shutdown_timers, [](const auto& timer) { return timer.expired(); });
        shutdown_timers.push_back(timer);
    }

    void RequestStop() {
        stopping.store(true, std::memory_order_release);
        for (const auto& weak : shutdown_timers) {
            if (auto timer = weak.lock()) timer->cancel();
        }
        notifier.Wake();
    }

    [[nodiscard]] bool StopRequested() const noexcept {
        return stopping.load(std::memory_order_acquire);
    }

    // Each committed file is acknowledged and published before the next one;
    // FlushCommitted restores only the uncommitted suffix on failure.
    int FlushBacklog() {
        return backlog.FlushCommitted([&](auto& entries, const auto& acknowledge) {
            // The newest IDs in a file are exactly the rows it has just committed.
            return db_write.Insert(
                entries, acknowledge, [&](const WriterDatabase& file, int inserted) {
                    auto newest = file.ReadLogs(
                        ResolveLogFields(file.catalog()->log_column_info, {"*"}), {},
                        Database::LogOrder::kIdDescending, std::min(inserted, config.sse_limit), 0);

                    std::vector<nlohmann::json> rows;
                    for (auto& row : newest | std::views::reverse)
                        rows.push_back(std::move(row.values));

                    notifier.Publish(std::move(rows));
                });
        });
    }
};

}  // namespace loglite

#endif  // LOGLITE_CONTEXT_HPP_
