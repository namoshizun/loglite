#include "log_reader.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

namespace loglite {

// Partition ranges are disjoint and hold timestamps inside their range, so reading
// files newest-first, each in its own order, yields the global order. Whole files
// before the requested page are skipped by their row counts.
PaginatedQueryResult LogReader::Query(const std::vector<std::string>& fields,
                                      const std::vector<QueryFilter>& filters, int limit,
                                      int offset) const {
    const auto& scheme = store_.scheme();
    // logs.db is the only file; SQLite pages it and estimates the unfiltered total.
    if (!scheme.partitioned()) return connection_.Query(fields, filters, limit, offset);
    const auto normalized = scheme.NormalizeFilters(filters);
    const auto resolved = connection_.ResolveFields(fields);
    connection_.ValidateFilters(normalized);

    PaginatedQueryResult result{0, offset, limit, {}};
    int64_t skip = offset;
    const auto page_full = [&] {
        return limit >= 0 && result.results.size() >= static_cast<size_t>(limit);
    };
    const auto lease = store_.ReadLease();
    const auto files = store_.Partitions();
    for (const auto& file : files | std::views::reverse) {
        if (!scheme.MayContain(file.partition, normalized)) continue;
        if (normalized.empty() && (page_full() || skip >= file.rows)) {
            result.total += file.rows;
            skip -= std::min(skip, file.rows);
            continue;
        }
        UseFile(file.partition.path, [&](const ReaderDatabase& db) {
            const int64_t count = normalized.empty() ? file.rows : db.CountLogs(normalized);
            result.total += count;
            if (page_full() || skip >= count) {
                skip -= std::min(skip, count);
                return;
            }
            const int remaining = limit < 0 ? -1 : limit - static_cast<int>(result.results.size());
            for (auto& row :
                 db.ReadLogs(resolved, normalized, ReaderDatabase::LogOrder::kNewestFirst,
                             remaining, static_cast<int>(skip)))
                result.results.push_back(std::move(row.values));
            skip = 0;
        });
    }
    return result;
}

// IDs interleave across files, but each file's first `limit` IDs in the range
// contain the global first `limit`.
LogIdQueryResult LogReader::QueryLogIdRange(const std::vector<std::string>& fields,
                                            int64_t since_exclusive, int64_t until_inclusive,
                                            int limit) const {
    const auto resolved = connection_.ResolveFields(fields);
    const std::vector<QueryFilter> range{{"id", ">", since_exclusive},
                                         {"id", "<=", until_inclusive}};
    std::vector<ReaderDatabase::LogRow> rows;
    {
        const auto lease = store_.ReadLease();
        for (const auto& file : store_.Partitions()) {
            if (file.id_upper_bound <= since_exclusive) continue;
            UseFile(file.partition.path, [&](const ReaderDatabase& db) {
                std::ranges::move(
                    db.ReadLogs(resolved, range, ReaderDatabase::LogOrder::kIdAscending, limit, 0),
                    std::back_inserter(rows));
            });
        }
    }
    std::ranges::sort(rows, {}, &ReaderDatabase::LogRow::id);
    if (limit >= 0 && rows.size() > static_cast<size_t>(limit)) rows.resize(limit);

    LogIdQueryResult result{rows.empty() ? since_exclusive : rows.back().id, {}};
    result.results.reserve(rows.size());
    for (auto& row : rows) result.results.push_back(std::move(row.values));
    return result;
}

}  // namespace loglite
