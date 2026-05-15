#include "backlog.hpp"
#include "metrics.hpp"

#include <utility>

namespace loglite {

Backlog::Backlog(size_t max_size) : max_size_(max_size) {}

void Backlog::Add(nlohmann::json log) {
    bool dropped = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (queue_.size() >= max_size_) {
            queue_.pop_front();
            dropped = true;
        }
        queue_.push_back(std::move(log));

        // Notify flush when the buffer is near full to avoid dropping logs.
        if (queue_.size() >= max_size_ * 0.95) {
            is_full_.store(true, std::memory_order_release);
        }
    }
    if (dropped) {
        metrics::MetricsRegistry::Instance().Collect(metrics::kBacklogDrop);
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

bool Backlog::IsFull() const noexcept { return is_full_.load(std::memory_order_acquire); }

size_t Backlog::Size() const {
    std::lock_guard lk(mtx_);
    return queue_.size();
}

}  // namespace loglite
