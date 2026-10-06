#ifndef LOGLITE_TASKS_VACUUM_HPP_
#define LOGLITE_TASKS_VACUUM_HPP_

#include "../context.hpp"
#include "../log.hpp"

#include <boost/asio.hpp>
#include <chrono>

namespace asio = boost::asio;

namespace loglite::tasks {

using namespace std::chrono_literals;

// ── Vacuum task ────────────────────────────────────────────────────────────────

inline asio::awaitable<void> VacuumTask(ServerContext& ctx) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = ctx.config;
    auto timer = std::make_shared<asio::steady_timer>(ex);
    ctx.RegisterShutdownTimer(timer);

    log::INFO("Vacuum task started (interval={}s)", cfg.task_vacuum_interval);

    while (!ctx.StopRequested()) {
        timer->expires_after(cfg.task_vacuum_interval * 1s);
        co_await timer->async_wait(asio::as_tuple(asio::use_awaitable));

        if (ctx.StopRequested()) {
            log::INFO("[Termination] vacuum task stopped");
            co_return;
        }

        // All vacuum operations mutate the DB → run on write strand.
        co_await ctx.db_write.AsyncUseConnection(ctx.write_strand,
                                                 [](LogStore& store) { store.Maintain(); });
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_VACUUM_HPP_
