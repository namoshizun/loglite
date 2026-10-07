#ifndef LOGLITE_TASKS_VACUUM_HPP_
#define LOGLITE_TASKS_VACUUM_HPP_

#include "../log.hpp"
#include "../runtime.hpp"
#include "../schedule.hpp"

#include <boost/asio.hpp>
#include <chrono>

namespace asio = boost::asio;

namespace loglite::tasks {

using namespace std::chrono_literals;

inline asio::awaitable<void> VacuumTask(Runtime& runtime) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = runtime.config();
    auto timer = std::make_shared<asio::steady_timer>(ex);
    runtime.RegisterShutdownTimer(timer);

    log::INFO("Vacuum task started (interval={}s)", cfg.task_vacuum_interval);

    while (!runtime.StopRequested()) {
        timer->expires_after(cfg.task_vacuum_interval * 1s);
        co_await timer->async_wait(asio::as_tuple(asio::use_awaitable));

        if (runtime.StopRequested()) {
            log::INFO("[Termination] vacuum task stopped");
            co_return;
        }

        co_await Schedule(runtime.write_strand(), [&runtime] { runtime.Maintain(); });
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_VACUUM_HPP_
