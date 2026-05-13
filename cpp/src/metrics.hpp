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

using std::chrono_literals::operator""s;

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
    static MetricsRegistry& Instance();

    void Configure(std::chrono::milliseconds window);
    void Collect(std::string_view name, double value = 0.0, int64_t item_count = 1);
    void IncrementGauge(std::string_view name);
    void DecrementGauge(std::string_view name);
    [[nodiscard]] int64_t Gauge(std::string_view name) const;
    [[nodiscard]] std::vector<Observation> Flush();
    void Reset(std::chrono::seconds window = 60s);

   private:
    MetricsRegistry();

    void prune(std::chrono::steady_clock::time_point now);

    mutable std::mutex mtx_;
    std::chrono::milliseconds window_{60s};
    std::deque<Observation> observations_;
    std::unordered_map<std::string_view, int64_t> gauges_;
};

class ObservationTimer {
   public:
    explicit ObservationTimer(std::string_view name);
    ~ObservationTimer();

    ObservationTimer(const ObservationTimer&) = delete;
    ObservationTimer& operator=(const ObservationTimer&) = delete;

   private:
    std::string_view name_;
    std::chrono::steady_clock::time_point started_at_{std::chrono::steady_clock::now()};
};

class GaugeGuard {
   public:
    explicit GaugeGuard(std::string_view name);
    ~GaugeGuard();

    GaugeGuard(const GaugeGuard&) = delete;
    GaugeGuard& operator=(const GaugeGuard&) = delete;

   private:
    std::string_view name_;
};

}  // namespace loglite::metrics

#endif  // LOGLITE_METRICS_HPP_
