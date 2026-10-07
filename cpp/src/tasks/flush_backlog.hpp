#ifndef LOGLITE_TASKS_FLUSH_BACKLOG_HPP_
#define LOGLITE_TASKS_FLUSH_BACKLOG_HPP_

#include "../log.hpp"
#include "../metrics.hpp"
#include "../runtime.hpp"
#include "../schedule.hpp"
#include "../utils.hpp"

#include <boost/asio.hpp>
#include <chrono>

namespace asio = boost::asio;

namespace loglite::tasks {

using namespace std::chrono_literals;

// Wakes when the backlog crosses its watermark, and also on the configured
// deadline. Stop does not drain: the runtime settles after producers finish.
inline asio::awaitable<void> FlushBacklogTask(Runtime& runtime) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = runtime.config();
    auto timer = std::make_shared<asio::steady_timer>(ex);
    runtime.RegisterShutdownTimer(timer);
    runtime.ingestion().SetWake([timer] { timer->cancel(); });

    log::INFO("Backlog flush task started");

    while (!runtime.StopRequested()) {
        if (!runtime.ingestion().ShouldFlush()) {
            timer->expires_after(cfg.task_backlog_flush_interval * 1s);
            co_await timer->async_wait(asio::as_tuple(asio::use_awaitable));
        }
        if (runtime.StopRequested()) {
            log::INFO("[Termination] backlog flush task stopped");
            co_return;
        }
        if (runtime.ingestion().size() == 0 && runtime.ingestion().in_flight_bytes() == 0) continue;

        auto [count, elapsed] = co_await Schedule(runtime.write_strand(), [&runtime] {
            Timer timer_scope;
            const int settled = runtime.Settle();
            return std::make_pair(settled, timer_scope.elapsed_ms());
        });
        if (count == 0) continue;

        metrics::MetricsRegistry::Instance().Collect(metrics::kInsertBatch, elapsed, count);
        log::DEBUG("Inserted {} row(s)", count);
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_FLUSH_BACKLOG_HPP_
