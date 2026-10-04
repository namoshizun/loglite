#ifndef LOGLITE_BACKLOG_HPP_
#define LOGLITE_BACKLOG_HPP_

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace loglite {

// ── Backlog ────────────────────────────────────────────────────────────────────
//
// Thread-safe, bounded in-memory buffer for incoming log entries.
// Logs are batched here and flushed to SQLite by a background task.
//
// When the queue is at capacity, Add() evicts the oldest entry before
// inserting the new one (drop-oldest policy), so memory use is bounded
// even if the flush task falls behind or dies.
//
// `IsFull()` is polled by the flush task so it can exit the periodic wait early
// when the queue crosses a ~95% high watermark — before drop-oldest triggers
// at the hard cap (task_backlog_max_size).

class Backlog {
   public:
    explicit Backlog(size_t max_size);

    void Add(nlohmann::json log);

    // Move all pending entries out of the backlog in one critical section.
    std::vector<nlohmann::json> Flush();

    // Lend the batch read-only to persistence; restore it on failure.
    // The callback must commit before returning.
    template <typename F>
    int Flush(F&& persist) {
        auto entries = Flush();
        if (entries.empty()) return 0;
        try {
            return std::invoke(std::forward<F>(persist), std::as_const(entries));
        } catch (...) {
            Restore(std::move(entries));
            throw;
        }
    }

    // Lend the batch mutably: persistence may reorder it, then acknowledges the
    // committed prefix of that order. A failure restores only the uncommitted rest.
    template <typename F>
    int FlushCommitted(F&& persist) {
        auto entries = Flush();
        if (entries.empty()) return 0;
        size_t committed = 0;
        const auto acknowledge = [&committed](size_t prefix) { committed = prefix; };
        try {
            return std::invoke(std::forward<F>(persist), entries, acknowledge);
        } catch (...) {
            entries.erase(entries.begin(), entries.begin() + committed);
            Restore(std::move(entries));
            throw;
        }
    }

    bool IsFull() const noexcept;

    size_t Size() const;

   private:
    void Restore(std::vector<nlohmann::json> entries);

    mutable std::mutex mtx_;
    std::deque<nlohmann::json> queue_;
    size_t max_size_;
    std::atomic<bool> is_full_{false};
};

}  // namespace loglite

#endif  // LOGLITE_BACKLOG_HPP_
