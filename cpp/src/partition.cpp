#include "partition.hpp"

#include "utils.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace loglite {

namespace {

constexpr auto kCalendarIntervals =
    std::to_array({PartitionInterval::kHourly, PartitionInterval::kDaily,
                   PartitionInterval::kWeekly, PartitionInterval::kMonthly});

struct CalendarRange {
    TimePoint since;
    TimePoint until;
    std::string filename;
};

CalendarRange Containing(PartitionInterval interval, TimePoint timestamp) {
    using namespace std::chrono;
    const auto name = ToString(interval);
    const sys_days day = floor<days>(timestamp);
    switch (interval) {
    case PartitionInterval::kHourly: {
        const auto hour = floor<hours>(timestamp);
        return {hour, hour + hours{1},
                fmt::format("logs-{}-{:%Y-%m-%dT%H}.db", name, date::sys_seconds{hour})};
    }
    case PartitionInterval::kDaily:
        return {day, day + days{1},
                fmt::format("logs-{}-{:%Y-%m-%d}.db", name, date::sys_seconds{day})};
    case PartitionInterval::kWeekly: {
        const sys_days start = day - days{(weekday{day}.iso_encoding() + 6) % 7};
        return {start, start + days{7},
                fmt::format("logs-{}-{:%Y-%m-%d}.db", name, date::sys_seconds{start})};
    }
    case PartitionInterval::kMonthly: {
        const year_month_day date{day};
        const sys_days start{date.year() / date.month() / 1};
        const sys_days end{(date.year() / date.month() + months{1}) / 1};
        return {start, end, fmt::format("logs-{}-{:%Y-%m}.db", name, date::sys_seconds{start})};
    }
    case PartitionInterval::kNone:
        break;
    }
    throw std::logic_error("Unpartitioned storage has no calendar ranges");
}

std::optional<CalendarRange> FromFilename(PartitionInterval interval, std::string_view filename) {
    const auto prefix = fmt::format("logs-{}-", ToString(interval));
    if (!filename.starts_with(prefix) || !filename.ends_with(".db")) return std::nullopt;
    std::string timestamp{filename.substr(prefix.size(), filename.size() - prefix.size() - 3)};
    timestamp += interval == PartitionInterval::kHourly    ? ":00:00Z"
                 : interval == PartitionInterval::kMonthly ? "-01T00:00:00Z"
                                                           : "T00:00:00Z";
    const auto parsed = parse_iso8601(timestamp);
    if (!parsed) return std::nullopt;
    auto range = Containing(interval, *parsed);
    if (range.filename != filename) return std::nullopt;
    return range;
}

std::optional<TimePoint> FilterInstant(const QueryFilter& filter, std::string_view field) {
    if (filter.field != field || filter.op == "~=" || !filter.value.is_string())
        return std::nullopt;
    return parse_iso8601(filter.value.get_ref<const std::string&>());
}

}  // namespace

Partition PartitionScheme::Place(nlohmann::json& entry, TimePoint ingestion) const {
    if (!partitioned()) return Route(ingestion);
    auto timestamp = ingestion;
    auto& value = entry[cfg_.log_timestamp_field];
    if (value.is_string())
        if (const auto parsed = parse_iso8601(value.get_ref<const std::string&>()))
            timestamp = *parsed;
    // Route the instant the stored text denotes, so the file range invariant holds.
    timestamp = std::chrono::floor<std::chrono::milliseconds>(timestamp);
    value = format_utc(timestamp);
    return Route(timestamp);
}

Partition PartitionScheme::Route(TimePoint timestamp) const {
    if (!partitioned()) return {cfg_.db_path};
    auto range = Containing(cfg_.partition_interval, timestamp);
    return {cfg_.sqlite_dir / range.filename, range.since, range.until};
}

std::optional<Partition> PartitionScheme::Parse(const std::filesystem::path& path) const {
    if (!partitioned()) return std::nullopt;
    const auto range = FromFilename(cfg_.partition_interval, path.filename().string());
    if (!range) return std::nullopt;
    return Partition{path, range->since, range->until};
}

std::vector<QueryFilter> PartitionScheme::NormalizeFilters(std::vector<QueryFilter> filters) const {
    if (!partitioned()) return filters;
    for (auto& filter : filters)
        if (const auto instant = FilterInstant(filter, cfg_.log_timestamp_field))
            filter.value = format_utc(*instant);
    return filters;
}

bool PartitionScheme::MayContain(const Partition& partition,
                                 const std::vector<QueryFilter>& filters) const {
    return std::ranges::all_of(filters, [&](const QueryFilter& filter) {
        const auto instant = FilterInstant(filter, cfg_.log_timestamp_field);
        if (!instant) return true;
        if (filter.op == ">" || filter.op == ">=") return *instant < partition.until;
        if (filter.op == "<") return partition.since < *instant;
        if (filter.op == "<=") return partition.since <= *instant;
        if (filter.op == "=") return partition.since <= *instant && *instant < partition.until;
        return true;
    });
}

std::optional<PartitionInterval> PartitionScheme::IntervalOf(std::string_view filename) {
    for (const auto interval : kCalendarIntervals)
        if (FromFilename(interval, filename)) return interval;
    return std::nullopt;
}

std::vector<PartitionRegistry::Entry> PartitionRegistry::Snapshot() const {
    std::lock_guard lock{mutex_};
    return entries_;
}

void PartitionRegistry::Add(Partition partition) {
    std::lock_guard lock{mutex_};
    if (std::ranges::find(entries_, partition.path, [](const Entry& e) -> const auto& {
            return e.partition.path;
        }) != entries_.end())
        return;
    const auto position = std::ranges::upper_bound(
        entries_, partition.since, {}, [](const Entry& e) { return e.partition.since; });
    entries_.insert(position, Entry{std::move(partition)});
}

void PartitionRegistry::Update(const std::filesystem::path& path, int64_t id_upper_bound,
                               int64_t row_delta) {
    std::lock_guard lock{mutex_};
    const auto entry = std::ranges::find(
        entries_, path, [](const Entry& e) -> const auto& { return e.partition.path; });
    if (entry == entries_.end()) throw std::logic_error("Unknown partition file: " + path.string());
    entry->id_upper_bound = std::max(entry->id_upper_bound, id_upper_bound);
    entry->rows += row_delta;
}

void PartitionRegistry::Erase(const std::filesystem::path& path) {
    std::lock_guard lock{mutex_};
    std::erase_if(entries_, [&](const Entry& e) { return e.partition.path == path; });
}

}  // namespace loglite
