#ifndef LOGLITE_APPEND_HPP_
#define LOGLITE_APPEND_HPP_

#include <chrono>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace loglite {

// One prepared row admitted to the volatile queue. Omitted keys are absent;
// an explicit JSON null is stored as null. `admitted_at` is fixed at admission
// and kept across retries.
struct PreparedEntry {
    size_t index{};
    nlohmann::json fields = nlohmann::json::object();
    std::chrono::system_clock::time_point instant{};
    std::chrono::system_clock::time_point admitted_at{};
    size_t bytes{};
};

enum class StorageFailure { kNone, kTransient, kDiskFull, kInvariant };

struct StoredRow {
    size_t index{};
    int64_t id{};
    nlohmann::json values;
};

struct RejectedRow {
    size_t index{};
    std::string reason;
};

// Outcome of one append. Indices are positions in the vector passed to Append,
// not positions after partition reordering.
struct AppendResult {
    std::vector<StoredRow> committed;
    std::vector<RejectedRow> rejected;
    std::vector<size_t> pending;
    StorageFailure failure{StorageFailure::kNone};
    std::string message;
};

class StorageError : public std::runtime_error {
   public:
    StorageError(StorageFailure failure, std::string message)
        : std::runtime_error(message), failure_(failure) {}

    [[nodiscard]] StorageFailure failure() const noexcept { return failure_; }

   private:
    StorageFailure failure_;
};

}  // namespace loglite

#endif  // LOGLITE_APPEND_HPP_
