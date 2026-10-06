#ifndef LOGLITE_PARTITION_HPP_
#define LOGLITE_PARTITION_HPP_

#include "config.hpp"
#include "types.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace loglite {

using TimePoint = std::chrono::system_clock::time_point;

// One SQLite file holding the logs whose timestamps fall in [since, until).
struct Partition {
    std::filesystem::path path;
    TimePoint since{TimePoint::min()};
    TimePoint until{TimePoint::max()};
};

// Maps log timestamps to files. Calendar ranges are UTC; weeks begin on Monday.
// Partitioned files store canonical UTC timestamps (format_utc) inside their range,
// so lexical order is chronological and files never overlap. Unpartitioned storage
// is one unbounded partition, logs.db, whose values are stored verbatim.
class PartitionScheme {
   public:
    explicit PartitionScheme(const Config& cfg) : cfg_(cfg) {}

    [[nodiscard]] bool partitioned() const noexcept {
        return cfg_.partition_interval != PartitionInterval::kNone;
    }

    // Rewrites the entry's timestamp to canonical UTC, using `ingestion` when it is
    // missing or unparseable, and returns the partition that owns the entry.
    Partition Place(nlohmann::json& entry, TimePoint ingestion) const;

    [[nodiscard]] Partition Route(TimePoint timestamp) const;

    // The partition stored in `path`, when it is a file of this scheme.
    [[nodiscard]] std::optional<Partition> Parse(const std::filesystem::path& path) const;

    // Timestamp filter values must use the stored canonical form to compare correctly.
    [[nodiscard]] std::vector<QueryFilter> NormalizeFilters(std::vector<QueryFilter> filters) const;

    // Whether rows matching normalized `filters` can exist in `partition`.
    [[nodiscard]] bool MayContain(const Partition& partition,
                                  const std::vector<QueryFilter>& filters) const;

    // The interval of any partition filename, so a changed interval can be rejected.
    static std::optional<PartitionInterval> IntervalOf(std::string_view filename);

   private:
    const Config& cfg_;
};

// Known partition files, oldest first. The writer keeps the bookkeeping exact for
// rows it inserts and deletes; readers take snapshots.
class PartitionRegistry {
   public:
    struct Entry {
        Partition partition;
        int64_t id_upper_bound{};  // no ID stored in the file exceeds this
        int64_t rows{};
    };

    [[nodiscard]] std::vector<Entry> Snapshot() const;
    void Add(Partition partition);  // no-op when already known
    void Update(const std::filesystem::path& path, int64_t id_upper_bound, int64_t row_delta);
    void Erase(const std::filesystem::path& path);

   private:
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};

}  // namespace loglite

#endif  // LOGLITE_PARTITION_HPP_
