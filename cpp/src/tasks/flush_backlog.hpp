#ifndef LOGLITE_TASKS_FLUSH_BACKLOG_HPP_
#define LOGLITE_TASKS_FLUSH_BACKLOG_HPP_

#include "../globals.hpp"
#include "../log.hpp"
#include "../metrics.hpp"
#include "../utils.hpp"

#include <boost/asio.hpp>
#include <chrono>
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

    log::INFO("Backlog flush task started");

    while (true) {
        // Poll every 100 ms; break early when the backlog hits the flush watermark.
        auto deadline = std::chrono::steady_clock::now() + cfg.task_backlog_flush_interval * 1s;
        while (std::chrono::steady_clock::now() < deadline && !ctx.backlog.IsFull()) {
            timer.expires_after(100ms);
            co_await timer.async_wait(asio::use_awaitable);
        }

        auto logs = ctx.backlog.Flush();
        if (logs.empty()) continue;

        log::DEBUG("Flushing {} log(s) from backlog", logs.size());

        auto [count, max_id, elapsed] =
            co_await ctx.db_write.AsyncUseConnection(ctx.write_strand, [&](WriterDatabase& db) {
                Timer t;
                int c = db.Insert(logs);
                int64_t m = db.GetMaxLogId();
                return std::make_tuple(c, m, t.elapsed_ms());
            });

        metrics::MetricsRegistry::Instance().Collect(metrics::kInsertBatch, elapsed, count);
        ctx.notifier.Notify(max_id);

        log::DEBUG("Inserted {} row(s), max_log_id={}", count, max_id);
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_FLUSH_BACKLOG_HPP_
