#ifndef LOGLITE_CONTEXT_HPP_
#define LOGLITE_CONTEXT_HPP_

#include "backlog.hpp"
#include "config.hpp"
#include "notifier.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include <boost/asio.hpp>

namespace asio = boost::asio;

namespace loglite {

// Aggregates all shared mutable state passed to handlers and background tasks.
// Passed by reference; must outlive all coroutines.

struct ServerContext {
    Config& config;
    WriterDatabase& db_write;
    ReadDatabasePool& db_read;
    Backlog& backlog;
    LogNotifier& notifier;

    asio::strand<asio::thread_pool::executor_type> write_strand;
    asio::thread_pool::executor_type reader_executor;
    std::chrono::steady_clock::time_point server_started_at;

    std::atomic<bool> stopping{false};
    std::vector<std::shared_ptr<asio::steady_timer>> shutdown_timers;

    ServerContext(Config& config_in, WriterDatabase& db_write_in, ReadDatabasePool& db_read_in,
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
        shutdown_timers.push_back(timer);
    }

    void RequestStop() {
        stopping.store(true, std::memory_order_release);
        for (const auto& timer : shutdown_timers) {
            timer->cancel();
        }
    }

    [[nodiscard]] bool StopRequested() const noexcept {
        return stopping.load(std::memory_order_acquire);
    }
};

}  // namespace loglite

#endif  // LOGLITE_CONTEXT_HPP_
