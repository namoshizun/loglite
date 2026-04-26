#ifndef LOGLITE_TASKS_VACUUM_HPP_
#define LOGLITE_TASKS_VACUUM_HPP_

#include "../globals.hpp"
#include "../log.hpp"
#include "../utils.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <format>

namespace asio = boost::asio;

namespace loglite::tasks {

namespace detail {

// Remove log entries older than max_age_days; returns deleted count.
inline int remove_stale_logs(Database& db, const Config& cfg) {
    using namespace std::chrono;

    auto min_ts = db.get_min_timestamp();
    if (min_ts.empty()) return 0;

    // Build cutoff ISO timestamp string (UTC).
    auto now = system_clock::now();
    auto cutoff = now - hours(24 * cfg.vacuum_max_days);
    auto cutoff_t = system_clock::to_time_t(cutoff);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&cutoff_t));

    // Only bother if the oldest log pre-dates the cutoff.
    if (min_ts >= std::string{buf}) return 0;

    std::vector<QueryFilter> flt{{cfg.log_timestamp_field, "<=", std::string{buf}}};
    int n = db.delete_logs(flt);
    if (n > 0)
        log::info(std::format("[vacuum] removed {} stale log(s) older than {} days", n,
                              cfg.vacuum_max_days));
    return n;
}

// Delete oldest logs until DB size is below target; returns deleted count.
inline int remove_excessive_logs(Database& db, const Config& cfg) {
    double db_mb = db.get_size_mb();
    double max_mb = bytes_to_mb(cfg.vacuum_max_size_bytes);
    double target_mb = bytes_to_mb(cfg.vacuum_target_size_bytes);

    if (db_mb <= max_mb) return 0;

    int64_t min_id = db.get_min_log_id();
    int64_t max_id = db.get_max_log_id();
    int64_t rowcnt = max_id - min_id + 1;

    double ratio = (db_mb - target_mb) / db_mb;
    int64_t remove_max_id = min_id + static_cast<int64_t>(rowcnt * ratio) - 1;

    log::info(
        std::format("[vacuum] db={:.1f}MB limit={:.1f}MB target={:.1f}MB – deleting id {} to {}",
                    db_mb, max_mb, target_mb, min_id, remove_max_id));

    int removed = 0;
    for (int64_t start = min_id; start <= remove_max_id; start += cfg.vacuum_delete_batch_size) {
        int64_t end_id = std::min(start + cfg.vacuum_delete_batch_size - 1, remove_max_id);
        std::vector<QueryFilter> flt{{"id", "<=", end_id}};
        removed += db.delete_logs(flt);
        log::info(std::format("[vacuum] ... removed {} entries so far", removed));
    }
    return removed;
}

inline int incremental_vacuum_pass(Database& db, int max_size_mb) {
    auto freelist = db.get_pragma("freelist_count");
    if (freelist.empty() || freelist == "0") return 0;

    int64_t fl = std::stoll(freelist);
    int64_t pgsz = std::stoll(db.get_pragma("page_size"));
    int64_t budget = static_cast<int64_t>(max_size_mb) * 1024 * 1024 / pgsz;
    int64_t pages = std::min(budget, fl);

    Timer t;
    db.incremental_vacuum(static_cast<int>(pages));
    log::info(
        std::format("[vacuum] incremental_vacuum({}) pages in {:.1f}s", pages, t.elapsed_s()));

    return static_cast<int>(std::stoll(db.get_pragma("freelist_count")));
}

}  // namespace detail

// ── Vacuum task ────────────────────────────────────────────────────────────────

inline asio::awaitable<void> vacuum_task(ServerContext& ctx) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = ctx.config;
    asio::steady_timer timer{ex};

    log::info(std::format("Vacuum task started (interval={}s)", cfg.task_vacuum_interval));

    while (true) {
        timer.expires_after(std::chrono::seconds(cfg.task_vacuum_interval));
        co_await timer.async_wait(asio::use_awaitable);

        // All vacuum operations mutate the DB → run on write strand.
        co_await asio::dispatch(asio::bind_executor(ctx.write_strand, asio::use_awaitable));

        ctx.db.wal_checkpoint("TRUNCATE");

        auto vacuum_mode_str = ctx.db.get_pragma("auto_vacuum");
        int vacuum_mode = vacuum_mode_str.empty() ? 0 : std::stoi(vacuum_mode_str);

        if (vacuum_mode == 2) {  // INCREMENTAL
            int remain = detail::incremental_vacuum_pass(ctx.db, cfg.task_vacuum_max_size);
            if (remain > 0) {
                co_await asio::post(asio::bind_executor(ex, asio::use_awaitable));
                continue;
            }
        }

        bool has_ts = std::ranges::any_of(ctx.db.column_info(), [&](const ColumnInfo& ci) {
            return ci.name == cfg.log_timestamp_field;
        });

        if (has_ts) detail::remove_stale_logs(ctx.db, cfg);

        detail::remove_excessive_logs(ctx.db, cfg);

        if (vacuum_mode == 1) {  // FULL
            Timer t;
            ctx.db.vacuum();
            ctx.db.wal_checkpoint("FULL");
            log::info(std::format("[vacuum] full vacuum completed in {:.1f}s", t.elapsed_s()));
        }

        co_await asio::post(asio::bind_executor(ex, asio::use_awaitable));
    }
}

}  // namespace loglite::tasks

#endif  // LOGLITE_TASKS_VACUUM_HPP_
