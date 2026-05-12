#ifndef LOGLITE_TASKS_DIAGNOSTICS_HPP_
#define LOGLITE_TASKS_DIAGNOSTICS_HPP_

#include "../globals.hpp"
#include "../log.hpp"
#include "../metrics.hpp"

#include <boost/asio.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <limits>
#include <string>
#include <vector>

namespace asio = boost::asio;

namespace loglite::tasks {

namespace detail {

struct ObservationSummary {
    int64_t sample_count{};
    int64_t item_count{};
    double value_total{};
    double min{};
    double max{};
    double avg{};
};

inline ObservationSummary summarize_observations(const std::vector<metrics::Observation>& samples,
                                                 std::string_view name) {
    ObservationSummary summary;
    double min_value = std::numeric_limits<double>::max();
    double max_value = std::numeric_limits<double>::lowest();

    for (const auto& sample : samples) {
        if (sample.name != name) continue;
        ++summary.sample_count;
        summary.item_count += sample.item_count;
        summary.value_total += sample.value;
        min_value = std::min(min_value, sample.value);
        max_value = std::max(max_value, sample.value);
    }

    if (summary.sample_count == 0) return summary;

    summary.min = min_value;
    summary.max = max_value;
    summary.avg = summary.value_total / static_cast<double>(summary.sample_count);
    return summary;
}

inline int64_t round_stat(double value) { return static_cast<int64_t>(std::llround(value)); }

inline std::string format_utc(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

inline ActivityStatsRow build_activity_stats(std::string since, std::string until,
                                             const std::vector<metrics::Observation>& samples) {
    auto query = summarize_observations(samples, metrics::kQueryRequest);
    auto ingest = summarize_observations(samples, metrics::kIngestRequest);
    auto drops = summarize_observations(samples, metrics::kBacklogDrop);
    auto inserts = summarize_observations(samples, metrics::kInsertBatch);

    auto& registry = metrics::MetricsRegistry::Instance();
    return {
        std::move(since),
        std::move(until),
        query.sample_count,
        round_stat(query.min),
        round_stat(query.max),
        round_stat(query.avg),
        ingest.sample_count,
        round_stat(ingest.min),
        round_stat(ingest.max),
        round_stat(ingest.avg),
        drops.item_count,
        inserts.sample_count,
        inserts.item_count,
        round_stat(inserts.value_total),
        registry.Gauge(metrics::kSseSession),
        registry.Gauge(metrics::kHttpConnection),
    };
}

}  // namespace detail

// ── Diagnostics task ───────────────────────────────────────────────────────────
//
// Periodically snapshots process-wide metrics, persists them, and prunes old stats rows.

inline asio::awaitable<void> DiagnosticsTask(ServerContext& ctx) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = ctx.config;
    asio::steady_timer timer{ex};
    auto window_since = std::chrono::system_clock::now();

    log::info(
        std::format("Diagnostics task started (interval={}s)", cfg.task_diagnostics_interval));

    while (true) {
        timer.expires_after(std::chrono::seconds(cfg.task_diagnostics_interval));
        co_await timer.async_wait(asio::use_awaitable);

        auto window_until = std::chrono::system_clock::now();
        auto samples = metrics::MetricsRegistry::Instance().SnapshotObservations();
        auto row = detail::build_activity_stats(detail::format_utc(window_since),
                                                detail::format_utc(window_until), samples);
        auto cutoff =
            detail::format_utc(window_until - std::chrono::hours{cfg.stats_retention_hours});

        co_await asio::dispatch(asio::bind_executor(ctx.write_strand, asio::use_awaitable));

        ctx.db.InsertActivityStats(row);
        ctx.db.InsertDatabaseStats({
            row.until,
            ctx.db.GetLogRowCount(),
            ctx.db.GetSizeBytes(),
        });
        int pruned = ctx.db.DeleteStatsBefore(cutoff);

        co_await asio::post(asio::bind_executor(ex, asio::use_awaitable));

        log::info(std::format(
            "[stats] query: count={} avg={}ms max={}ms | "
            "ingest: count={} avg_size={}B drops={} | "
            "insert: batches={} rows={} total={}ms | "
            "sse_sessions={} http_connections={} pruned={}",
            row.query_count, row.query_avg, row.query_max, row.ingest_count, row.ingest_size_avg,
            row.ingest_drop_count, row.insert_batch_count, row.insert_total_count,
            row.insert_total_cost, row.sse_session_count, row.http_conn_count, pruned));

        window_since = window_until;
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_DIAGNOSTICS_HPP_
