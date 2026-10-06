#include "log_store.hpp"

#include "log.hpp"
#include "utils.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <limits>
#include <set>

namespace loglite {

namespace {

// A file as seen by one maintenance pass, oldest first.
struct Candidate {
    PartitionRegistry::Entry entry;
    int64_t bytes{};
    bool expired{};
};

// Spends up to `budget` bytes of freed pages; returns whether freed pages remain.
bool ReclaimPages(WriterDatabase& db, int64_t& budget) {
    const int64_t page_size = std::stoll(db.GetPragma("page_size"));
    const int64_t free_pages = std::stoll(db.GetPragma("freelist_count"));
    const int64_t pages = std::min({free_pages, budget / page_size, int64_t{INT_MAX}});

    if (pages > 0) {
        Timer timer;
        db.IncrementalVacuum(static_cast<int>(pages));
        budget -= pages * page_size;
        log::INFO("[vacuum] IncrementalVacuum({}) pages in {:.1f}s", pages, timer.elapsed_s());
    }

    return std::stoll(db.GetPragma("freelist_count")) > 0;
}

}  // namespace

// Partition files are whole time ranges, so retention unlinks files that lie
// entirely past a limit and trims rows only in the one boundary file. Without
// partitioning, logs.db is that boundary file and is never removed.
void LogStore::Maintain() {
    writer_.reset();

    const int vacuum_mode = std::stoi(control_.GetPragma("auto_vacuum"));
    const bool incremental = vacuum_mode == 2;
    const int64_t pass_bytes = static_cast<int64_t>(cfg_.task_vacuum_max_size) * 1024 * 1024;
    const bool removable = scheme_.partitioned();

    // Finish reclaiming previously freed pages before deleting another batch.
    // The pass budget belongs to logical storage, so it is shared by all files.
    int64_t vacuum_budget = pass_bytes > 0 ? pass_bytes : std::numeric_limits<int64_t>::max();
    bool reclaiming = false;

    std::vector<Candidate> files;
    for (auto& entry : registry_.Snapshot()) {
        auto& file = files.emplace_back(Candidate{std::move(entry)});
        UseFile(file.entry.partition.path, [&](WriterDatabase& db) {
            if (incremental) reclaiming |= ReclaimPages(db, vacuum_budget);
            file.bytes = db.GetSizeBytes();
        });
    }
    if (reclaiming) return;

    int64_t rows = 0;
    int64_t bytes = 0;
    for (const auto& file : files) {
        rows += file.entry.rows;
        bytes += file.bytes;
    }

    const int64_t average_bytes = std::max<int64_t>(1, bytes / std::max<int64_t>(1, rows));
    int64_t delete_budget = incremental && pass_bytes > 0
                                ? std::max<int64_t>(1, pass_bytes / average_bytes)
                                : std::numeric_limits<int64_t>::max();

    std::set<std::filesystem::path> trimmed;
    // Applies `remove` to one file and keeps the registry and budget in step.
    const auto trim = [&](Candidate& file, auto&& remove) {
        int64_t removed = 0;
        UseFile(file.entry.partition.path, [&](WriterDatabase& db) {
            removed = remove(db);
            if (removed > 0) file.bytes = db.GetSizeBytes();
        });

        if (removed == 0) return removed;
        file.entry.rows -= removed;
        delete_budget -= removed;
        registry_.Update(file.entry.partition.path, 0, -removed);
        trimmed.insert(file.entry.partition.path);
        return removed;
    };

    // Age: expired files go whole; rows only expire inside the boundary file.
    const auto expiry =
        std::chrono::system_clock::now() - std::chrono::hours{24LL * cfg_.vacuum_max_days};
    const auto cutoff = format_iso_seconds(expiry);
    const auto& columns = control_.GetColumnInfo();
    const bool has_timestamp =
        std::ranges::find(columns, cfg_.log_timestamp_field, &ColumnInfo::name) != columns.end();

    int64_t stale = 0;
    for (auto& file : files) {
        if (file.entry.partition.since > expiry) break;
        if (removable && file.entry.partition.until <= expiry) {
            file.expired = true;
            continue;
        }
        if (has_timestamp && delete_budget > 0)
            stale += trim(
                file, [&](WriterDatabase& db) { return db.DeleteOldLogs(cutoff, delete_budget); });
    }

    if (stale > 0)
        log::INFO("[vacuum] removed {} stale log(s) older than {} days", stale,
                  cfg_.vacuum_max_days);

    // Size: drop whole files oldest first, then trim the oldest rows of the next one.
    int64_t live_bytes = 0;
    for (const auto& file : files)
        if (!file.expired) live_bytes += file.bytes;

    if (live_bytes > cfg_.vacuum_max_size_bytes && delete_budget > 0) {
        int64_t excess = live_bytes - cfg_.vacuum_target_size_bytes;
        log::INFO("[vacuum] db={:.1f}MB limit={:.1f}MB target={:.1f}MB", bytes_to_mb(live_bytes),
                  bytes_to_mb(cfg_.vacuum_max_size_bytes),
                  bytes_to_mb(cfg_.vacuum_target_size_bytes));

        for (auto& file : files) {
            if (file.expired || file.entry.rows == 0) continue;
            if (excess <= 0) break;
            if (removable && file.bytes <= excess) {
                file.expired = true;
                excess -= file.bytes;
                continue;
            }

            const long double ratio = std::clamp(
                static_cast<long double>(excess) / std::max<int64_t>(1, file.bytes), 0.0L, 1.0L);
            const int64_t count = std::min(
                delete_budget, std::max<int64_t>(1, static_cast<int64_t>(file.entry.rows * ratio)));
            const int64_t removed =
                trim(file, [&](WriterDatabase& db) { return db.DeleteOldestLogs(count); });
            log::INFO("[vacuum] ... removed {} oldest log(s)", removed);
            break;
        }
    }

    // Files are unlinked after all accounting so a deferred removal retries next pass.
    std::vector<std::filesystem::path> unlinked;
    for (const auto& file : files)
        if (removable && (file.expired || file.entry.rows == 0))
            unlinked.push_back(file.entry.partition.path);
    RemoveFiles(unlinked);

    if (vacuum_mode == 1) {
        for (const auto& path : trimmed) {
            if (std::ranges::find(unlinked, path) != unlinked.end()) continue;
            UseFile(path, [&](WriterDatabase& db) {
                Timer timer;
                db.Vacuum();
                db.WALCheckpoint("FULL");
                log::INFO("[vacuum] full vacuum of {} completed in {:.1f}s",
                          path.filename().string(), timer.elapsed_s());
            });
        }
    }
}

void LogStore::RemoveFiles(const std::vector<std::filesystem::path>& paths) {
    if (paths.empty()) return;

    // Readers hold the lease for a whole query. Waiting here would stall the write
    // strand, so files in use are removed by a later pass.
    std::unique_lock lease{lease_, std::try_to_lock};
    if (!lease) {
        log::DEBUG("[vacuum] partition files in use; deferring removal of {} file(s)",
                   paths.size());
        return;
    }

    for (const auto& path : paths) {
        registry_.Erase(path);
        for (const auto* suffix : {"", "-wal", "-shm"})
            std::filesystem::remove(path.string() + suffix);
        log::INFO("[vacuum] removed partition file {}", path.filename().string());
    }
}

}  // namespace loglite
