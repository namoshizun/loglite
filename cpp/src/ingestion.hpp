#ifndef LOGLITE_INGESTION_HPP_
#define LOGLITE_INGESTION_HPP_

#include "backlog.hpp"
#include "log_store.hpp"
#include "notifier.hpp"

#include <atomic>
#include <chrono>
#include <string>

#include <nlohmann/json.hpp>

namespace loglite {

struct SubmitResult {
    bool admitted{false};
    std::string reason;
};

// Owns normalization, admission, batching, settlement and retry. Every source
// submits here. Persistence acknowledgement is recorded before live-feed
// publication, so a publication failure cannot return a committed row.
class Ingestion {
   public:
    Ingestion(LogStore& store, LogNotifier& live, size_t capacity,
              std::chrono::seconds retry_backoff);

    [[nodiscard]] SubmitResult Submit(nlohmann::json entry);
    int Settle();
    void Seal();

    [[nodiscard]] bool sealed() const noexcept;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] std::vector<nlohmann::json> queued() const;
    [[nodiscard]] bool ShouldFlush() const;
    void SetWake(std::function<void()> wake);

    [[nodiscard]] size_t queued_bytes() const { return backlog_.QueuedBytes(); }
    [[nodiscard]] size_t in_flight_bytes() const { return backlog_.InFlightBytes(); }

   private:
    LogStore& store_;
    LogNotifier& live_;
    Backlog backlog_;
    std::chrono::seconds retry_backoff_;
    std::atomic<bool> sealed_{false};
    std::atomic<std::chrono::steady_clock::time_point> retry_after_{};
};

}  // namespace loglite

#endif  // LOGLITE_INGESTION_HPP_
