#include "backlog.hpp"
#include "metrics.hpp"

#include <stdexcept>
#include <utility>

namespace loglite {

Backlog::Backlog(size_t max_size) : max_size_(max_size) {
    if (max_size_ == 0) {
        throw std::invalid_argument("Backlog capacity must be at least 1");
    }
}

void Backlog::Add(nlohmann::json log) {
    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.push_back(std::move(log));
        dropped = GuardOverflow();
    }

    if (dropped) {
        metrics::MetricsRegistry::Instance().Collect(metrics::kBacklogDrop, 0, dropped);
    }
}

std::vector<nlohmann::json> Backlog::Flush() {
    std::lock_guard lk(mtx_);
    is_full_.store(false, std::memory_order_relaxed);

    std::vector<nlohmann::json> out(std::make_move_iterator(queue_.begin()),
                                    std::make_move_iterator(queue_.end()));
    queue_.clear();
    return out;
}

void Backlog::Restore(std::vector<nlohmann::json> entries) {
    size_t dropped = 0;
    {
        std::lock_guard lk(mtx_);
        // Failed entries precede anything added while the database was busy.
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            queue_.push_front(std::move(*it));
        }

        dropped = GuardOverflow();
    }

    if (dropped) {
        metrics::MetricsRegistry::Instance().Collect(metrics::kBacklogDrop, 0, dropped);
    }
}

size_t Backlog::GuardOverflow() {
    const size_t current_size = queue_.size();
    if (current_size <= max_size_) {
        is_full_.store(current_size >= max_size_ * 0.95, std::memory_order_release);
        return 0;
    }

    const size_t drop_count = current_size - max_size_;
    queue_.erase(queue_.begin(), queue_.begin() + drop_count);
    is_full_.store(true, std::memory_order_release);
    return drop_count;
}

bool Backlog::IsFull() const noexcept { return is_full_.load(std::memory_order_acquire); }

size_t Backlog::Size() const {
    std::lock_guard lk(mtx_);
    return queue_.size();
}

}  // namespace loglite
