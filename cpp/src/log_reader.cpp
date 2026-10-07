#include "log_reader.hpp"

#include <algorithm>

#include <fmt/format.h>
#include <iterator>
#include <ranges>
#include <stdexcept>

namespace loglite {

PaginatedQueryResult LogReader::Query(const std::vector<std::string>& fields,
                                      const std::vector<QueryFilter>& filters, int limit,
                                      int offset) const {
    QueryRequest request{fields, {}, limit, offset};
    request.predicates.reserve(filters.size());
    for (const auto& filter : filters) {
        const auto op = ParseCmpOp(filter.op);
        if (!op) throw std::runtime_error(fmt::format("Unknown query operator: '{}'", filter.op));
        request.predicates.push_back({filter.field, *op, filter.value});
    }
    return Query(PrepareQuery(store_.schema(), request));
}

// Partition ranges are disjoint and hold timestamps inside their range, so reading
// files newest-first, each in its own order, yields the global order.
PaginatedQueryResult LogReader::Query(const QueryPlan& plan) const {
    std::vector<QueryFilter> filters;
    filters.reserve(plan.predicates.size());
    for (const auto& predicate : plan.predicates) {
        auto stored = TranslatePredicate(predicate, store_.schema());
        if (stored.impossible) return {0, plan.offset, plan.limit, {}};
        filters.push_back(std::move(stored.filter));
    }

    const auto& scheme = store_.scheme();
    if (!scheme.partitioned())
        return connection_.Query(plan.fields, filters, plan.limit, plan.offset);

    PaginatedQueryResult result{0, plan.offset, plan.limit, {}};
    int64_t skip = plan.offset;
    const auto page_full = [&] {
        return plan.limit >= 0 && result.results.size() >= static_cast<size_t>(plan.limit);
    };

    const auto lease = store_.ReadLease();
    const auto files = store_.Partitions();

    for (const auto& file : files | std::views::reverse) {
        if (!scheme.MayContain(file.partition, filters)) continue;
        // Unfiltered totals are the maintained row counts, so a page that cannot
        // touch a file does not open it.
        if (filters.empty() && (page_full() || skip >= file.rows)) {
            result.total += file.rows;
            skip -= std::min(skip, file.rows);
            continue;
        }
        UseFile(file.partition.path, [&](const ReaderDatabase& db) {
            db.BindSnapshot(plan.fields, filters);
            const int64_t count = filters.empty() ? file.rows : db.CountLogs(filters);
            result.total += count;
            if (page_full() || skip >= count) {
                skip -= std::min(skip, count);
                return;
            }
            const int remaining =
                plan.limit < 0 ? -1 : plan.limit - static_cast<int>(result.results.size());
            for (auto& row : db.ReadLogs(plan.fields, filters, Database::LogOrder::kNewestFirst,
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

    std::vector<Database::LogRow> rows;
    {
        const auto lease = store_.ReadLease();
        for (const auto& file : store_.Partitions()) {
            if (file.id_upper_bound <= since_exclusive) continue;
            UseFile(file.partition.path, [&](const ReaderDatabase& db) {
                std::ranges::move(
                    db.ReadLogs(resolved, range, Database::LogOrder::kIdAscending, limit, 0),
                    std::back_inserter(rows));
            });
        }
    }

    std::ranges::sort(rows, {}, &Database::LogRow::id);
    if (limit >= 0 && rows.size() > static_cast<size_t>(limit)) rows.resize(limit);

    LogIdQueryResult result{rows.empty() ? since_exclusive : rows.back().id, {}};
    result.results.reserve(rows.size());
    std::ranges::transform(rows, std::back_inserter(result.results),
                           [](auto& row) { return std::move(row.values); });
    return result;
}

}  // namespace loglite
