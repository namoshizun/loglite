#include "ingestion.hpp"

#include "log.hpp"
#include "prepare.hpp"

namespace loglite {

Ingestion::Ingestion(LogStore& store, LogNotifier& live, size_t capacity,
                     std::chrono::seconds retry_backoff)
    : store_(store), live_(live), backlog_(capacity), retry_backoff_(retry_backoff) {}

SubmitResult Ingestion::Submit(nlohmann::json entry) {
    if (sealed_.load(std::memory_order_acquire)) return {false, "ingestion is closed"};
    if (!store_.schema().usable()) return {false, "log schema is not ready"};

    const auto admitted_at = std::chrono::system_clock::now();
    auto preparation = PrepareEntries(store_.schema(), {std::move(entry)}, admitted_at);
    if (!preparation.rejected.empty()) return {false, preparation.rejected.front().reason};

    backlog_.Add(std::move(preparation.admitted.front()));
    return {true, {}};
}

int Ingestion::Settle() {
    auto batch = backlog_.Take();
    if (batch.empty()) return 0;

    size_t bytes = 0;
    for (size_t index = 0; index < batch.size(); ++index) {
        bytes += batch[index].bytes;
        batch[index].index = index;
    }

    AppendResult result;
    try {
        result = store_.Append(batch);
    } catch (...) {
        backlog_.NoteSettled(batch.size(), bytes);
        backlog_.Restore(std::move(batch));
        throw;
    }
    backlog_.NoteSettled(batch.size(), bytes);

    std::vector<PreparedEntry> pending;
    pending.reserve(result.pending.size());
    for (const size_t index : result.pending) pending.push_back(std::move(batch[index]));
    if (!pending.empty()) backlog_.Restore(std::move(pending));

    if (result.failure == StorageFailure::kTransient)
        retry_after_.store(std::chrono::steady_clock::now() + retry_backoff_,
                           std::memory_order_release);

    std::vector<nlohmann::json> published;
    published.reserve(result.committed.size());
    for (auto& row : result.committed) published.push_back(std::move(row.values));
    try {
        live_.Publish(std::move(published));
    } catch (const std::exception& error) {
        log::ERROR("live feed publication failed after commit: {}", error.what());
    }

    if (result.failure == StorageFailure::kDiskFull ||
        result.failure == StorageFailure::kInvariant) {
        Seal();
        throw StorageError{result.failure, result.message};
    }
    return static_cast<int>(result.committed.size());
}

void Ingestion::Seal() { sealed_.store(true, std::memory_order_release); }

bool Ingestion::sealed() const noexcept { return sealed_.load(std::memory_order_acquire); }

size_t Ingestion::size() const { return backlog_.Size(); }

std::vector<nlohmann::json> Ingestion::queued() const { return backlog_.QueuedFields(); }

bool Ingestion::ShouldFlush() const {
    const auto retry = retry_after_.load(std::memory_order_acquire);
    if (std::chrono::steady_clock::now() < retry) return false;
    return backlog_.IsFull();
}

void Ingestion::SetWake(std::function<void()> wake) { backlog_.SetWake(std::move(wake)); }

}  // namespace loglite
