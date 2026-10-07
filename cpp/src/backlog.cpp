#include "backlog.hpp"

#include "metrics.hpp"

namespace loglite {

Backlog::Backlog(size_t max_size) : max_size_(max_size) {
    if (max_size_ == 0) throw std::invalid_argument("Backlog capacity must be at least 1");
}

void Backlog::SetWake(std::function<void()> wake) {
    std::lock_guard lock{mtx_};
    wake_ = std::move(wake);
}

void Backlog::Add(PreparedEntry entry) {
    size_t dropped = 0;
    std::function<void()> wake;
    {
        std::lock_guard lock{mtx_};
        const bool was_full = is_full_.load(std::memory_order_relaxed);
        queued_bytes_ += entry.bytes;
        queue_.push_back(std::move(entry));
        dropped = GuardOverflow();
        if (!was_full && is_full_.load(std::memory_order_relaxed)) wake = wake_;
    }

    if (dropped) metrics::MetricsRegistry::Instance().Collect(metrics::kBacklogDrop, 0, dropped);
    if (wake) wake();
}

std::vector<PreparedEntry> Backlog::Take() {
    std::lock_guard lock{mtx_};
    std::vector<PreparedEntry> out(std::make_move_iterator(queue_.begin()),
                                   std::make_move_iterator(queue_.end()));
    queue_.clear();
    in_flight_entries_ += out.size();
    in_flight_bytes_ += queued_bytes_;
    queued_bytes_ = 0;
    is_full_.store(in_flight_entries_ >= max_size_, std::memory_order_release);
    return out;
}

void Backlog::Restore(std::vector<PreparedEntry> entries) {
    size_t dropped = 0;
    {
        std::lock_guard lock{mtx_};
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            queued_bytes_ += it->bytes;
            queue_.push_front(std::move(*it));
        }
        dropped = GuardOverflow();
    }
    if (dropped) metrics::MetricsRegistry::Instance().Collect(metrics::kBacklogDrop, 0, dropped);
}

void Backlog::NoteSettled(size_t entries, size_t bytes) {
    std::lock_guard lock{mtx_};
    in_flight_entries_ -= std::min(in_flight_entries_, entries);
    in_flight_bytes_ -= std::min(in_flight_bytes_, bytes);
    const size_t occupancy = queue_.size() + in_flight_entries_;
    is_full_.store(max_size_ > 0 && occupancy >= max_size_ * 95 / 100, std::memory_order_release);
}

bool Backlog::IsFull() const noexcept { return is_full_.load(std::memory_order_acquire); }

size_t Backlog::Size() const {
    std::lock_guard lock{mtx_};
    return queue_.size();
}

size_t Backlog::QueuedBytes() const {
    std::lock_guard lock{mtx_};
    return queued_bytes_;
}

size_t Backlog::InFlightEntries() const {
    std::lock_guard lock{mtx_};
    return in_flight_entries_;
}

size_t Backlog::InFlightBytes() const {
    std::lock_guard lock{mtx_};
    return in_flight_bytes_;
}

std::vector<nlohmann::json> Backlog::QueuedFields() const {
    std::lock_guard lock{mtx_};
    std::vector<nlohmann::json> fields;
    fields.reserve(queue_.size());
    for (const auto& entry : queue_) fields.push_back(entry.fields);
    return fields;
}

size_t Backlog::GuardOverflow() {
    size_t dropped = 0;
    while (queue_.size() + in_flight_entries_ > max_size_) {
        if (queue_.empty()) break;
        const bool drop_newest = queue_.size() == 1;
        auto& victim = drop_newest ? queue_.back() : queue_.front();
        queued_bytes_ -= std::min(queued_bytes_, victim.bytes);
        if (drop_newest)
            queue_.pop_back();
        else
            queue_.pop_front();
        ++dropped;
    }

    const size_t occupancy = queue_.size() + in_flight_entries_;
    is_full_.store(occupancy * 100 >= max_size_ * 95, std::memory_order_release);
    return dropped;
}

}  // namespace loglite
