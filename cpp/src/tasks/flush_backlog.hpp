#ifndef LOGLITE_TASKS_FLUSH_BACKLOG_HPP_
#define LOGLITE_TASKS_FLUSH_BACKLOG_HPP_

#include "../globals.hpp"
#include "../log.hpp"
#include "../metrics.hpp"
#include "../utils.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <format>
#include <thread>

namespace asio = boost::asio;

namespace loglite::tasks {

using namespace std::chrono_literals;

// ── Backlog flush task ─────────────────────────────────────────────────────────
//
// Runs as an infinite Asio coroutine.  Every task_backlog_flush_interval seconds
// (or when Backlog signals the high watermark via IsFull()), it:
//   1. Drains the backlog.
//   2. Dispatches to the write strand to INSERT into SQLite.
//   3. Reads max_log_id and notifies SSE subscribers.

inline asio::awaitable<void> FlushBacklogTask(ServerContext& ctx) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = ctx.config;
    asio::steady_timer timer{ex};

    log::info("Backlog flush task started");

    while (true) {
        // Poll every 100 ms; break early when the backlog hits the flush watermark.
        auto deadline = std::chrono::steady_clock::now() + cfg.task_backlog_flush_interval * 1s;
        while (std::chrono::steady_clock::now() < deadline && !ctx.backlog.IsFull()) {
            timer.expires_after(100ms);
            co_await timer.async_wait(asio::use_awaitable);
        }

        auto logs = ctx.backlog.Flush();
        if (logs.empty()) continue;

        if (cfg.debug) log::debug(std::format("Flushing {} log(s) from backlog", logs.size()));

        // Serialise DB writes through the strand.
        co_await asio::dispatch(asio::bind_executor(ctx.write_strand, asio::use_awaitable));

        Timer t;
        int count = ctx.db.Insert(logs);
        int64_t max = ctx.db.GetMaxLogId();

        // Leave the strand by posting back to the generic pool executor.
        co_await asio::post(asio::bind_executor(ex, asio::use_awaitable));

        metrics::MetricsRegistry::Instance().Collect(metrics::kInsertBatch, t.elapsed_ms(), count);
        ctx.notifier.Notify(max);

        if (cfg.debug) log::debug(std::format("Inserted {} row(s), max_log_id={}", count, max));
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_FLUSH_BACKLOG_HPP_
