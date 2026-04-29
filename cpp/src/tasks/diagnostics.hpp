#ifndef LOGLITE_TASKS_DIAGNOSTICS_HPP_
#define LOGLITE_TASKS_DIAGNOSTICS_HPP_

#include "../globals.hpp"
#include "../log.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <format>

namespace asio = boost::asio;

namespace loglite::tasks {

// ── Diagnostics task ───────────────────────────────────────────────────────────
//
// Periodically logs ingestion and query statistics then resets the counters.

inline asio::awaitable<void> DiagnosticsTask(ServerContext& ctx) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = ctx.config;
    asio::steady_timer timer{ex};

    log::info(
        std::format("Diagnostics task started (interval={}s)", cfg.task_diagnostics_interval));

    while (true) {
        timer.expires_after(std::chrono::seconds(cfg.task_diagnostics_interval));
        co_await timer.async_wait(asio::use_awaitable);

        auto ing = ctx.ingest_stats.get_and_reset();
        auto qry = ctx.query_stats.get_and_reset();

        log::info(
            std::format("[stats] ingestion: count={} total={:.1f}ms avg={:.1f}ms max={:.1f}ms | "
                        "query: count={} total={:.1f}ms avg={:.1f}ms max={:.1f}ms | "
                        "sse_subscribers={}",
                        ing.count, ing.total_ms, ing.avg_ms, ing.max_ms, qry.count, qry.total_ms,
                        qry.avg_ms, qry.max_ms, ctx.notifier.SubscriberCount()));
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_DIAGNOSTICS_HPP_
