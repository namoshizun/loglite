#ifndef LOGLITE_RUNTIME_HPP_
#define LOGLITE_RUNTIME_HPP_

#include "access.hpp"
#include "submission.hpp"

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace loglite {

// Owns object lifetimes, executors, readiness, producer registration and
// shutdown for one server run.
class Runtime {
   public:
    explicit Runtime(Config config);
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    [[nodiscard]] Config& config() noexcept { return cfg_; }
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }
    [[nodiscard]] LogStore& store() noexcept { return store_; }
    [[nodiscard]] LogReaderPool& readers() noexcept { return *readers_; }
    [[nodiscard]] asio::thread_pool& read_pool() noexcept { return read_pool_; }
    [[nodiscard]] Ingestion& ingestion() noexcept { return *ingestion_; }
    [[nodiscard]] LogNotifier& live() noexcept { return live_; }
    [[nodiscard]] QueryService& queries() noexcept { return *queries_; }
    [[nodiscard]] HttpAccess http();

    [[nodiscard]] asio::strand<asio::thread_pool::executor_type>& write_strand() noexcept {
        return write_strand_;
    }
    [[nodiscard]] Submission submission() const { return Submission{gate_}; }
    [[nodiscard]] bool StopRequested() const noexcept {
        return stopping_.load(std::memory_order_acquire);
    }

    void RequestStop();
    void RegisterShutdownTimer(const std::shared_ptr<asio::steady_timer>& timer);
    int Settle();
    void Maintain();

   private:
    Config cfg_;
    LogStore store_;
    LogNotifier live_;
    asio::thread_pool write_pool_{1};
    asio::thread_pool read_pool_;
    asio::strand<asio::thread_pool::executor_type> write_strand_;
    std::unique_ptr<LogReaderPool> readers_;
    std::unique_ptr<Ingestion> ingestion_;
    std::unique_ptr<QueryService> queries_;
    std::shared_ptr<Submission::Gate> gate_;
    std::atomic<bool> stopping_{false};
    std::chrono::steady_clock::time_point started_at_{std::chrono::steady_clock::now()};
    std::vector<std::weak_ptr<asio::steady_timer>> shutdown_timers_;
};

}  // namespace loglite

#endif  // LOGLITE_RUNTIME_HPP_
