#ifndef LOGLITE_GLOBALS_HPP_
#define LOGLITE_GLOBALS_HPP_

#include "backlog.hpp"
#include "config.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/asio.hpp>

namespace asio = boost::asio;

namespace loglite {

// ── Log notifier ───────────────────────────────────────────────────────────────
//
// Pub-sub hub for SSE subscribers.  Each SSE handler registers a shared_ptr to
// an asio::steady_timer.  When new logs are flushed, notify() atomically updates
// last_id and cancels all subscriber timers.
//
// asio::steady_timer::cancel() is documented thread-safe (it posts through the
// io_context), so it's correct to call from the flush task's thread.

class LogNotifier {
   public:
    struct Subscription {
        // Shared so the notifier can cancel safely even if the handler is gone.
        std::shared_ptr<asio::steady_timer> timer;
    };

    [[nodiscard]] std::shared_ptr<Subscription> Subscribe(asio::any_io_executor ex);

    void Unsubscribe(const std::shared_ptr<Subscription>& sub);

    void Notify(int64_t id);

    int64_t GetLastId() const noexcept;

    size_t SubscriberCount() const;

   private:
    std::atomic<int64_t> last_id_{0};
    mutable std::mutex mtx_;
    std::vector<std::shared_ptr<Subscription>> subs_;
};

// ── Server context ─────────────────────────────────────────────────────────────
//
// Aggregates all shared mutable state passed to handlers and background tasks.
// Passed by reference; must outlive all coroutines.

struct ServerContext {
    Config& config;
    WriterDatabase& db_write;
    ReadDatabasePool& db_read;
    Backlog& backlog;
    LogNotifier& notifier;

    asio::strand<asio::thread_pool::executor_type> write_strand;
};

}  // namespace loglite

#endif  // LOGLITE_GLOBALS_HPP_
