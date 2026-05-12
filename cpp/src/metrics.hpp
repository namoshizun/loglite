#ifndef LOGLITE_METRICS_HPP_
#define LOGLITE_METRICS_HPP_

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace loglite::metrics {

inline constexpr std::string_view kQueryRequest = "query_request";
inline constexpr std::string_view kIngestRequest = "ingest_request";
inline constexpr std::string_view kBacklogDrop = "backlog_drop";
inline constexpr std::string_view kInsertBatch = "insert_batch";
inline constexpr std::string_view kHttpConnection = "http_connection";
inline constexpr std::string_view kSseSession = "sse_session";

struct Observation {
    std::chrono::steady_clock::time_point at;
    std::string_view name;
    double value{};
    int64_t item_count{1};
};

class MetricsRegistry {
   public:
    static MetricsRegistry& Instance() {
        static MetricsRegistry registry;
        return registry;
    }

    void Configure(std::chrono::milliseconds window) {
        std::lock_guard lk(mtx_);
        window_ = window;
        PruneLocked(std::chrono::steady_clock::now());
    }

    void Collect(std::string_view name, double value = 0.0, int64_t item_count = 1) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard lk(mtx_);
        PruneLocked(now);
        observations_.push_back({now, name, value, item_count});
    }

    void IncrementGauge(std::string_view name) {
        std::lock_guard lk(mtx_);
        ++gauges_[name];
    }

    void DecrementGauge(std::string_view name) {
        std::lock_guard lk(mtx_);
        --gauges_[name];
    }

    [[nodiscard]] int64_t Gauge(std::string_view name) const {
        std::lock_guard lk(mtx_);
        auto it = gauges_.find(name);
        return it == gauges_.end() ? 0 : it->second;
    }

    [[nodiscard]] std::vector<Observation> SnapshotObservations() {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard lk(mtx_);
        PruneLocked(now);
        return {observations_.begin(), observations_.end()};
    }

    void ResetForTest() {
        std::lock_guard lk(mtx_);
        observations_.clear();
        gauges_.clear();
        window_ = std::chrono::seconds{60};
    }

   private:
    MetricsRegistry() = default;

    void PruneLocked(std::chrono::steady_clock::time_point now) {
        while (!observations_.empty() && now - observations_.front().at >= window_) {
            observations_.pop_front();
        }
    }

    mutable std::mutex mtx_;
    std::chrono::milliseconds window_{std::chrono::seconds{60}};
    std::deque<Observation> observations_;
    std::unordered_map<std::string_view, int64_t> gauges_;
};

class ScopedObservationTimer {
   public:
    explicit ScopedObservationTimer(std::string_view name) : name_(name) {}

    ~ScopedObservationTimer() {
        auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                 started_at_)
                           .count();
        MetricsRegistry::Instance().Collect(name_, elapsed);
    }

    ScopedObservationTimer(const ScopedObservationTimer&) = delete;
    ScopedObservationTimer& operator=(const ScopedObservationTimer&) = delete;

   private:
    std::string_view name_;
    std::chrono::steady_clock::time_point started_at_{std::chrono::steady_clock::now()};
};

class GaugeGuard {
   public:
    explicit GaugeGuard(std::string_view name) : name_(name) {
        MetricsRegistry::Instance().IncrementGauge(name_);
    }

    ~GaugeGuard() { MetricsRegistry::Instance().DecrementGauge(name_); }

    GaugeGuard(const GaugeGuard&) = delete;
    GaugeGuard& operator=(const GaugeGuard&) = delete;

   private:
    std::string_view name_;
};

}  // namespace loglite::metrics

#endif  // LOGLITE_METRICS_HPP_
