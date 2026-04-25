#pragma once

#include "../globals.hpp"
#include "../log.hpp"
#include "../utils.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <format>
#include <thread>

namespace asio = boost::asio;

namespace loglite::tasks {

// ── Backlog flush task ─────────────────────────────────────────────────────────
//
// Runs as an infinite Asio coroutine.  Every task_backlog_flush_interval seconds
// (or immediately when the backlog is full), it:
//   1. Drains the backlog.
//   2. Dispatches to the write strand to INSERT into SQLite.
//   3. Reads max_log_id and notifies SSE subscribers.

inline asio::awaitable<void> flush_backlog_task(ServerContext& ctx) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = ctx.config;
    asio::steady_timer timer{ex};

    log::info("Backlog flush task started");

    while (true) {
        // Poll every 100 ms; break early when the backlog is full.
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(cfg.task_backlog_flush_interval);
        while (std::chrono::steady_clock::now() < deadline && !ctx.backlog.is_full()) {
            timer.expires_after(std::chrono::milliseconds(100));
            co_await timer.async_wait(asio::use_awaitable);
        }

        auto logs = ctx.backlog.flush();
        if (logs.empty()) continue;

        if (cfg.debug) log::debug(std::format("Flushing {} log(s) from backlog", logs.size()));

        // Serialise DB writes through the strand.
        co_await asio::dispatch(asio::bind_executor(ctx.write_strand, asio::use_awaitable));

        Timer t;
        int count = ctx.db.insert(logs);
        int64_t max = ctx.db.get_max_log_id();

        // Leave the strand by posting back to the generic pool executor.
        co_await asio::post(asio::bind_executor(ex, asio::use_awaitable));

        ctx.ingest_stats.collect(count, t.elapsed_ms());
        ctx.notifier.notify(max);

        if (cfg.debug) log::debug(std::format("Inserted {} row(s), max_log_id={}", count, max));
    }
}

}  // namespace loglite::tasks
