#ifndef LOGLITE_BACKLOG_HPP_
#define LOGLITE_BACKLOG_HPP_

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

#include <nlohmann/json.hpp>

namespace loglite {

// ── Backlog ────────────────────────────────────────────────────────────────────
//
// Thread-safe in-memory buffer for incoming log entries.
// Logs are batched here and flushed to SQLite by a background task.
//
// The `IsFull()` flag is polled by the flush task so it can force an early
// flush when the backlog reaches capacity (task_backlog_max_size).

class Backlog {
   public:
    explicit Backlog(size_t max_size);

    void Add(nlohmann::json log);

    // Move all pending entries out of the backlog in one critical section.
    std::vector<nlohmann::json> Flush();

    bool IsFull() const noexcept;

    size_t Size() const;

   private:
    mutable std::mutex mtx_;
    std::deque<nlohmann::json> queue_;
    size_t max_size_;
    std::atomic<bool> is_full_{false};
};

}  // namespace loglite

#endif  // LOGLITE_BACKLOG_HPP_
