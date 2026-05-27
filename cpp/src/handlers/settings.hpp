#ifndef LOGLITE_HANDLERS_SETTINGS_HPP_
#define LOGLITE_HANDLERS_SETTINGS_HPP_

#include "common.hpp"
#include "../context.hpp"

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace asio = boost::asio;

namespace loglite::handlers {

inline void AppendSetting(nlohmann::json& settings, std::string key, nlohmann::json value,
                          std::string description) {
    settings.push_back({{"key", std::move(key)},
                        {"value", std::move(value)},
                        {"description", std::move(description)}});
}

inline nlohmann::json BuildSettingsPayload(const Config& cfg) {
    nlohmann::json settings = nlohmann::json::array();

    AppendSetting(settings, "log_table_name", cfg.log_table_name,
                  "SQLite table name used to store log records.");
    AppendSetting(settings, "log_timestamp_field", cfg.log_timestamp_field,
                  "Column used for time-based retention and vacuum (ISO-8601 timestamps).");

    nlohmann::json sqlite_params = nlohmann::json::object();
    for (const auto& [k, v] : cfg.sqlite_params) {
        sqlite_params[k] = v;
    }
    AppendSetting(settings, "sqlite_params", sqlite_params,
                  "SQLite PRAGMA key/value pairs applied when opening the database.");

    AppendSetting(settings, "db_pool_size", cfg.db_pool_size, "Reader connection pool size");

    AppendSetting(settings, "auto_rollout", cfg.auto_rollout,
                  "Whether pending migrations are applied automatically on server startup.");

    AppendSetting(settings, "task_diagnostics_interval", cfg.task_diagnostics_interval,
                  "Seconds between activity and database stats collection passes.");
    AppendSetting(settings, "task_vacuum_interval", cfg.task_vacuum_interval,
                  "Seconds between incremental SQLite vacuum passes.");
    AppendSetting(settings, "task_backlog_flush_interval", cfg.task_backlog_flush_interval,
                  "Seconds between backlog flush passes to SQLite.");
    AppendSetting(settings, "task_backlog_max_size", cfg.task_backlog_max_size,
                  "Maximum backlog entries before a forced flush to the database.");
    AppendSetting(settings, "task_vacuum_max_size", cfg.task_vacuum_max_size,
                  "Megabyte budget per incremental vacuum pass.");

    AppendSetting(settings, "vacuum_max_days", cfg.vacuum_max_days,
                  "Drop log rows older than this many days during vacuum.");
    AppendSetting(settings, "vacuum_max_size", cfg.vacuum_max_size,
                  "Trigger vacuum when the database file exceeds this size.");
    AppendSetting(settings, "vacuum_target_size", cfg.vacuum_target_size,
                  "After vacuum, trim oldest rows until the database is under this size.");
    AppendSetting(settings, "stats_retention_hours", cfg.stats_retention_hours,
                  "Hours to retain collected stats rows before pruning.");

    AppendSetting(settings, "compression_enabled", cfg.compression.enabled,
                  "Whether dictionary compression is enabled for configured log columns.");

    nlohmann::json harvester_types = nlohmann::json::array();
    for (const auto& h : cfg.harvesters) {
        harvester_types.push_back(h.type);
    }
    AppendSetting(settings, "harvester_types", harvester_types,
                  "Harvester implementation types configured for this instance.");

    return {{"settings", settings}};
}

template <class Body>
asio::awaitable<http::response<http::string_body>> HandleSettings(const http::request<Body>& req,
                                                                  ServerContext& ctx) {
    co_return MakeOKResp(BuildSettingsPayload(ctx.config), req, ctx.config.allow_origin);
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_SETTINGS_HPP_
