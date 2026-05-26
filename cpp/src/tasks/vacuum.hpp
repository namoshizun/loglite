#ifndef LOGLITE_TASKS_VACUUM_HPP_
#define LOGLITE_TASKS_VACUUM_HPP_

#include "../globals.hpp"
#include "../log.hpp"
#include "../utils.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <limits>

namespace asio = boost::asio;

namespace loglite::tasks {

using namespace std::chrono_literals;

namespace detail {

// Remove log entries older than max_age_days; returns deleted count.
// When limit_mb > 0, at most ~limit_mb MB worth of rows are removed per call.
inline int remove_stale_logs(WriterDatabase& db, const Config& cfg, int limit_mb) {
    auto min_ts = db.GetMinTimestamp();
    if (min_ts.empty()) return 0;

    // Build cutoff ISO timestamp string (UTC).
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - (cfg.vacuum_max_days * 24) * 1h;
    auto cutoff_t = std::chrono::system_clock::to_time_t(cutoff);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&cutoff_t));
    std::string cutoff_str{buf};

    // Only bother if the oldest log pre-dates the cutoff.
    if (min_ts >= cutoff_str) return 0;

    std::vector<QueryFilter> flt{{cfg.log_timestamp_field, "<=", cutoff_str}};

    if (limit_mb > 0) {
        // Control how much data to actually delete
        int64_t remove_max_id = db.GetMaxLogId();
        int64_t avg_row_bytes = db.EstimateAvgRowBytes();
        int64_t max_rows = static_cast<int64_t>(limit_mb) * 1024 * 1024 / avg_row_bytes;
        remove_max_id = std::min(remove_max_id, db.GetMinLogId() + max_rows - 1);

        flt.push_back({"id", "<=", remove_max_id});
    }

    int n = db.DeleteLogs(flt);
    if (n > 0)
        log::INFO("[vacuum] removed {} stale log(s) older than {} days", n, cfg.vacuum_max_days);
    return n;
}

// Delete oldest logs until DB size is below target; returns deleted count.
// When limit_mb > 0, at most ~limit_mb MB worth of rows are removed per call.
inline int remove_excessive_logs(WriterDatabase& db, const Config& cfg, int limit_mb) {
    double db_mb = db.GetSizeMB();
    double max_mb = bytes_to_mb(cfg.vacuum_max_size_bytes);
    double target_mb = bytes_to_mb(cfg.vacuum_target_size_bytes);
    if (db_mb <= max_mb) return 0;

    int64_t min_id = db.GetMinLogId();
    int64_t max_id = db.GetMaxLogId();
    int64_t rowcnt = max_id - min_id + 1;

    double ratio = (db_mb - target_mb) / db_mb;
    int64_t remove_max_id = min_id + static_cast<int64_t>(rowcnt * ratio) - 1;

    if (limit_mb > 0) {
        int64_t avg_row_bytes = db.EstimateAvgRowBytes();
        int64_t max_rows = static_cast<int64_t>(limit_mb) * 1024 * 1024 / avg_row_bytes;
        remove_max_id = std::min(remove_max_id, min_id + max_rows - 1);
    }

    log::INFO("[vacuum] db={:.1f}MB limit={:.1f}MB target={:.1f}MB – deleting id {} to {}", db_mb,
              max_mb, target_mb, min_id, remove_max_id);

    std::vector<QueryFilter> flt{{"id", "<=", remove_max_id}};
    int removed = db.DeleteLogs(flt);
    log::INFO("[vacuum] ... removed {} entries", removed);
    return removed;
}

inline int incremental_vacuum_pass(WriterDatabase& db, int max_size_mb) {
    auto freelist = db.GetPragma("freelist_count");
    if (freelist.empty() || freelist == "0") return 0;

    int64_t fl = std::stoll(freelist);
    int64_t pgsz = std::stoll(db.GetPragma("page_size"));
    int64_t budget = static_cast<int64_t>(max_size_mb) * 1024 * 1024 / pgsz;
    int64_t pages = std::min(budget, fl);

    Timer t;
    db.IncrementalVacuum(static_cast<int>(pages));
    log::INFO("[vacuum] IncrementalVacuum({}) pages in {:.1f}s", pages, t.elapsed_s());

    return static_cast<int>(std::stoll(db.GetPragma("freelist_count")));
}

}  // namespace detail

using namespace detail;

// ── Vacuum task ────────────────────────────────────────────────────────────────

inline asio::awaitable<void> VacuumTask(ServerContext& ctx) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = ctx.config;
    asio::steady_timer timer{ex};

    log::INFO("Vacuum task started (interval={}s)", cfg.task_vacuum_interval);

    while (true) {
        timer.expires_after(cfg.task_vacuum_interval * 1s);
        co_await timer.async_wait(asio::use_awaitable);

        // All vacuum operations mutate the DB → run on write strand.
        co_await asio::dispatch(asio::bind_executor(ctx.write_strand, asio::use_awaitable));

        auto vacuum_mode_str = ctx.db_write.GetPragma("auto_vacuum");
        int vacuum_mode = vacuum_mode_str.empty() ? 0 : std::stoi(vacuum_mode_str);

        int limit_mb = (vacuum_mode == 2) ? cfg.task_vacuum_max_size : 0;

        if (vacuum_mode == 2) {  // INCREMENTAL
            int remain = incremental_vacuum_pass(ctx.db_write, cfg.task_vacuum_max_size);
            if (remain > 0) {
                co_await asio::post(asio::bind_executor(ex, asio::use_awaitable));
                continue;
            }
        }

        bool has_ts = std::ranges::any_of(ctx.db_write.GetColumnInfo(), [&](const ColumnInfo& ci) {
            return ci.name == cfg.log_timestamp_field;
        });

        if (has_ts) remove_stale_logs(ctx.db_write, cfg, limit_mb);

        remove_excessive_logs(ctx.db_write, cfg, limit_mb);

        if (vacuum_mode == 1) {  // FULL
            Timer t;
            ctx.db_write.Vacuum();
            ctx.db_write.WALCheckpoint("FULL");
            log::INFO("[vacuum] full vacuum completed in {:.1f}s", t.elapsed_s());
        }

        co_await asio::post(asio::bind_executor(ex, asio::use_awaitable));
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_VACUUM_HPP_
