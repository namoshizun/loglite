#ifndef LOGLITE_PREPARE_HPP_
#define LOGLITE_PREPARE_HPP_

#include "append.hpp"
#include "schema.hpp"

#include <vector>

#include <nlohmann/json.hpp>

namespace loglite {

struct RejectedInput {
    size_t index{};
    std::string reason;
};

struct Preparation {
    std::vector<PreparedEntry> admitted;
    std::vector<RejectedInput> rejected;
};

// One preparation path for every source. Partition routing is not done here;
// the returned instant is what routing consumes, and only partitioned storage
// rewrites the stored timestamp text.
[[nodiscard]] Preparation PrepareEntries(const LogSchema& schema,
                                         const std::vector<nlohmann::json>& entries,
                                         std::chrono::system_clock::time_point admitted_at);

}  // namespace loglite

#endif  // LOGLITE_PREPARE_HPP_
