#ifndef LOGLITE_BACKLOG_HPP_
#define LOGLITE_BACKLOG_HPP_

#include "append.hpp"

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace loglite {

// Thread-safe bounded queue of prepared entries. Only the ingestion coordinator
// writes it. When queued plus in-flight entries exceed capacity, the oldest
// queued entry is evicted. In-flight entries are already out of the queue and
// are not evicted.
class Backlog {
   public:
    explicit Backlog(size_t max_size);

    void Add(PreparedEntry entry);
    void SetWake(std::function<void()> wake);

    // Move every queued entry out and count it as in flight.
    std::vector<PreparedEntry> Take();
    void Restore(std::vector<PreparedEntry> entries);
    void NoteSettled(size_t entries, size_t bytes);

    [[nodiscard]] bool IsFull() const noexcept;
    [[nodiscard]] size_t Size() const;
    [[nodiscard]] size_t QueuedBytes() const;
    [[nodiscard]] size_t InFlightEntries() const;
    [[nodiscard]] size_t InFlightBytes() const;
    [[nodiscard]] std::vector<nlohmann::json> QueuedFields() const;

   private:
    size_t GuardOverflow();

    mutable std::mutex mtx_;
    std::deque<PreparedEntry> queue_;
    size_t max_size_;
    size_t queued_bytes_{};
    size_t in_flight_entries_{};
    size_t in_flight_bytes_{};
    std::atomic<bool> is_full_{false};
    std::function<void()> wake_;
};

}  // namespace loglite

#endif  // LOGLITE_BACKLOG_HPP_
