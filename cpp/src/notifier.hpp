#ifndef LOGLITE_NOTIFIER_HPP_
#define LOGLITE_NOTIFIER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

namespace asio = boost::asio;

namespace loglite {

// In-process ring of recently committed rows, ordered by commit (oldest first).
class LogNotifier {
   public:
    using Record = std::shared_ptr<const nlohmann::json>;

    explicit LogNotifier(size_t capacity) : capacity_(capacity) {}
    virtual ~LogNotifier() = default;

    // Returns the cursor at registration: only later publications are delivered.
    [[nodiscard]] uint64_t Subscribe(std::shared_ptr<asio::steady_timer> timer) {
        std::lock_guard lock(mtx_);
        timers_.push_back(std::move(timer));
        return published_;
    }

    void Unsubscribe(const std::shared_ptr<asio::steady_timer>& timer) {
        std::lock_guard lock(mtx_);
        std::erase(timers_, timer);
    }

    virtual void Publish(std::vector<nlohmann::json> rows) {
        if (rows.empty()) return;
        std::lock_guard lock(mtx_);

        for (auto& row : rows)
            recent_.push_back(std::make_shared<const nlohmann::json>(std::move(row)));
        published_ += rows.size();

        while (recent_.size() > capacity_) recent_.pop_front();
        WakeLocked();
    }

    void Wake() {
        std::lock_guard lock(mtx_);
        WakeLocked();
    }

    // Rows published after `cursor`, oldest first; evicted rows are lost.
    [[nodiscard]] std::vector<Record> Since(uint64_t& cursor) const {
        std::lock_guard lock(mtx_);

        const uint64_t first = published_ - recent_.size();
        const auto skip = static_cast<std::ptrdiff_t>(std::max(cursor, first) - first);

        std::vector<Record> rows(recent_.begin() + skip, recent_.end());
        cursor = published_;
        return rows;
    }

    [[nodiscard]] size_t SubscriberCount() const {
        std::lock_guard lock(mtx_);
        return timers_.size();
    }

   private:
    void WakeLocked() {
        for (const auto& timer : timers_)
            asio::post(timer->get_executor(), [timer] { timer->cancel(); });
    }

    const size_t capacity_;
    mutable std::mutex mtx_;
    std::deque<Record> recent_;
    uint64_t published_{0};
    std::vector<std::shared_ptr<asio::steady_timer>> timers_;
};

}  // namespace loglite

#endif  // LOGLITE_NOTIFIER_HPP_
