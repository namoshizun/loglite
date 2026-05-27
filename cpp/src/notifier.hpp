#ifndef LOGLITE_NOTIFIER_HPP_
#define LOGLITE_NOTIFIER_HPP_

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/asio.hpp>

namespace asio = boost::asio;

namespace loglite {

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

    [[nodiscard]] std::shared_ptr<Subscription> Subscribe(asio::any_io_executor ex) {
        auto sub =
            std::make_shared<Subscription>(std::make_shared<asio::steady_timer>(std::move(ex)));
        std::lock_guard lk(mtx_);
        subs_.push_back(sub);
        return sub;
    }

    void Unsubscribe(const std::shared_ptr<Subscription>& sub) {
        std::lock_guard lk(mtx_);
        std::erase(subs_, sub);
    }

    void Notify(int64_t id) {
        last_id_.store(id, std::memory_order_release);
        std::lock_guard lk(mtx_);
        for (auto& sub : subs_)
            sub->timer->cancel();  // posts cancellation through io_context – thread-safe
    }

    int64_t GetLastId() const noexcept { return last_id_.load(std::memory_order_acquire); }

    size_t SubscriberCount() const {
        std::lock_guard lk(mtx_);
        return subs_.size();
    }

   private:
    std::atomic<int64_t> last_id_{0};
    mutable std::mutex mtx_;
    std::vector<std::shared_ptr<Subscription>> subs_;
};

}  // namespace loglite

#endif  // LOGLITE_NOTIFIER_HPP_
