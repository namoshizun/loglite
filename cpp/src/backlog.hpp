#pragma once

#include <atomic>
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
// The `is_full()` flag is polled by the flush task so it can force an early
// flush when the backlog reaches capacity (task_backlog_max_size).

class Backlog {
public:
    explicit Backlog(size_t max_size) : max_size_(max_size) {}

    void add(nlohmann::json log) {
        std::lock_guard lk(mtx_);
        queue_.push_back(std::move(log));
        if (queue_.size() >= max_size_)
            is_full_.store(true, std::memory_order_release);
    }

    // Move all pending entries out of the backlog in one critical section.
    std::vector<nlohmann::json> flush() {
        std::lock_guard lk(mtx_);
        is_full_.store(false, std::memory_order_relaxed);
        std::vector<nlohmann::json> out(
            std::make_move_iterator(queue_.begin()),
            std::make_move_iterator(queue_.end()));
        queue_.clear();
        return out;
    }

    bool is_full() const noexcept {
        return is_full_.load(std::memory_order_acquire);
    }

    size_t size() const {
        std::lock_guard lk(mtx_);
        return queue_.size();
    }

private:
    mutable std::mutex          mtx_;
    std::deque<nlohmann::json>  queue_;
    size_t                      max_size_;
    std::atomic<bool>           is_full_{false};
};

} // namespace loglite
