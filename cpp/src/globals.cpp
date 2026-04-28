#include "globals.hpp"

#include <algorithm>

namespace loglite {

std::shared_ptr<LogNotifier::Subscription> LogNotifier::Subscribe(asio::any_io_executor ex) {
    auto sub = std::make_shared<Subscription>(std::make_shared<asio::steady_timer>(std::move(ex)));
    std::lock_guard lk(mtx_);
    subs_.push_back(sub);
    return sub;
}

void LogNotifier::Unsubscribe(const std::shared_ptr<Subscription>& sub) {
    std::lock_guard lk(mtx_);
    std::erase(subs_, sub);
}

void LogNotifier::Notify(int64_t id) {
    last_id_.store(id, std::memory_order_release);
    std::lock_guard lk(mtx_);
    for (auto& sub : subs_)
        sub->timer->cancel();  // posts cancellation through io_context – thread-safe
}

int64_t LogNotifier::GetLastId() const noexcept { return last_id_.load(std::memory_order_acquire); }

size_t LogNotifier::SubscriberCount() const {
    std::lock_guard lk(mtx_);
    return subs_.size();
}

}  // namespace loglite
