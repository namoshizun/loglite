#ifndef LOGLITE_TASKS_DIAGNOSTICS_HPP_
#define LOGLITE_TASKS_DIAGNOSTICS_HPP_

#include "../globals.hpp"
#include "../log.hpp"
#include "../metrics.hpp"
#include "../utils.hpp"

#include <boost/asio.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <concepts>
#include <fmt/format.h>
#include <limits>
#include <string>
#include <vector>

namespace asio = boost::asio;

namespace loglite::tasks {

using namespace std::chrono_literals;

namespace detail {

struct ObservationSummary {
    int64_t sample_count{};
    int64_t item_count{};
    double value_total{};
    double min{};
    double max{};
    double avg{};
};

template <typename... Names>
    requires(sizeof...(Names) > 0 && (std::convertible_to<Names, std::string_view> && ...))
inline auto summarize_observations(const std::vector<metrics::Observation>& samples,
                                   Names&&... names)
    -> std::array<ObservationSummary, sizeof...(Names)> {
    constexpr std::size_t N = sizeof...(Names);
    const std::array<std::string_view, N> name_arr{std::string_view{names}...};

    // Initialize the summary entries
    std::array<ObservationSummary, N> out{};
    for (auto& s : out) {
        s.min = std::numeric_limits<double>::max();
        s.max = std::numeric_limits<double>::lowest();
    }

    for (const auto& sample : samples) {
        for (std::size_t i = 0; i < N; ++i) {
            if (name_arr[i] == sample.name) {
                auto& s = out[i];
                s.sample_count += 1;
                s.item_count += sample.item_count;
                s.value_total += sample.value;
                s.min = std::min(s.min, sample.value);
                s.max = std::max(s.max, sample.value);
                break;
            }
        }
    }

    for (auto& s : out) {
        if (s.sample_count > 0) {
            s.avg = s.value_total / static_cast<double>(s.sample_count);
        } else {
            s.min = 0.0;
            s.max = 0.0;
        }
    }
    return out;
}

inline int64_t round_stat(double value) { return static_cast<int64_t>(std::llround(value)); }

inline ActivityStatsRow build_activity_stats(std::string since, std::string until,
                                             const std::vector<metrics::Observation>& samples) {
    const auto [query, ingest, drops, inserts] =
        summarize_observations(samples, metrics::kQueryRequest, metrics::kIngestRequest,
                               metrics::kBacklogDrop, metrics::kInsertBatch);

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
        fmt::format("Diagnostics task started (interval={}s)", cfg.task_diagnostics_interval));

    while (true) {
        timer.expires_after(cfg.task_diagnostics_interval * 1s);
        co_await timer.async_wait(asio::use_awaitable);

        auto window_until = std::chrono::system_clock::now();
        auto samples = metrics::MetricsRegistry::Instance().Flush();
        auto row = detail::build_activity_stats(loglite::format_utc(window_since),
                                                loglite::format_utc(window_until), samples);
        auto cutoff = loglite::format_utc(window_until - cfg.stats_retention_hours * 1h);
        window_since = window_until;

        co_await asio::dispatch(asio::bind_executor(ctx.write_strand, asio::use_awaitable));

        ctx.db_write.InsertActivityStats(row);
        ctx.db_write.InsertDatabaseStats({
            row.until,
            ctx.db_write.EstimateLogRowCount(),
            ctx.db_write.GetSizeBytes(),
        });
        int pruned = ctx.db_write.DeleteStatsBefore(cutoff);

        co_await asio::post(asio::bind_executor(ex, asio::use_awaitable));

        log::info(fmt::format(
            "[query]: count={} avg={}ms max={}ms | "
            "[ingest]: count={} avg_size={}B drops={} | "
            "[insert]: batches={} rows={} total={}ms | "
            "sse_sessions={} http_conns={} pruned={}",
            row.query_count, row.query_avg, row.query_max, row.ingest_count, row.ingest_size_avg,
            row.ingest_drop_count, row.insert_batch_count, row.insert_total_count,
            row.insert_total_cost, row.sse_session_count, row.http_conn_count, pruned));
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_DIAGNOSTICS_HPP_
