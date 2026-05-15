#include "metrics.hpp"

#include <iterator>

namespace loglite::metrics {

// ── MetricsRegistry ──────────────────────────────────────────────────────────

MetricsRegistry::MetricsRegistry() = default;

MetricsRegistry& MetricsRegistry::Instance() {
    static MetricsRegistry registry;
    return registry;
}

void MetricsRegistry::Configure(std::chrono::milliseconds window) {
    std::lock_guard lk(mtx_);
    window_ = window;
    prune(std::chrono::steady_clock::now());
}

void MetricsRegistry::Collect(std::string_view name, double value, int64_t item_count) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lk(mtx_);
    prune(now);
    observations_.push_back({now, name, value, item_count});
}

void MetricsRegistry::IncrementGauge(std::string_view name) {
    std::lock_guard lk(mtx_);
    ++gauges_[name];
}

void MetricsRegistry::DecrementGauge(std::string_view name) {
    std::lock_guard lk(mtx_);
    --gauges_[name];
}

int64_t MetricsRegistry::Gauge(std::string_view name) const {
    std::lock_guard lk(mtx_);
    auto it = gauges_.find(name);
    return it == gauges_.end() ? 0 : it->second;
}

std::vector<Observation> MetricsRegistry::Flush() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lk(mtx_);
    prune(now);
    std::vector<Observation> out(std::make_move_iterator(observations_.begin()),
                                 std::make_move_iterator(observations_.end()));
    observations_.clear();
    return out;
}

void MetricsRegistry::Reset(std::chrono::seconds window) {
    std::lock_guard lk(mtx_);
    observations_.clear();
    gauges_.clear();
    window_ = window;
}

void MetricsRegistry::prune(std::chrono::steady_clock::time_point now) {
    while (!observations_.empty() && now - observations_.front().at >= window_) {
        observations_.pop_front();
    }
}

// ── ObservationTimer ──────────────────────────────────────────────────────────

ObservationTimer::ObservationTimer(std::string_view name) : name_(name) {}

ObservationTimer::~ObservationTimer() {
    using std::chrono_literals::operator""ms;
    const double elapsed = (std::chrono::steady_clock::now() - started_at_) / 1.0ms;
    MetricsRegistry::Instance().Collect(name_, elapsed);
}

// ── GaugeGuard ───────────────────────────────────────────────────────────────
GaugeGuard::GaugeGuard(std::string_view name) : name_(name) {
    MetricsRegistry::Instance().IncrementGauge(name_);
}

GaugeGuard::~GaugeGuard() { MetricsRegistry::Instance().DecrementGauge(name_); }

}  // namespace loglite::metrics
