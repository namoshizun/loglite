#pragma once

#include "types.hpp"
#include "utils.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace loglite {

struct Config {
    // ── Server ────────────────────────────────────────────────────────────────
    std::string host{"127.0.0.1"};
    uint16_t    port{7788};
    bool        debug{false};
    std::string allow_origin{"*"};

    // ── Database ──────────────────────────────────────────────────────────────
    std::filesystem::path             sqlite_dir{"./db"};
    std::filesystem::path             db_path;  // derived
    std::map<std::string, std::string> sqlite_params;
    bool                              auto_rollout{false};

    // ── Log table ─────────────────────────────────────────────────────────────
    std::string log_table_name{"Log"};
    std::string log_timestamp_field{"timestamp"};

    // ── SSE ───────────────────────────────────────────────────────────────────
    int sse_limit{1000};
    int sse_debounce_ms{500};

    // ── Vacuum ────────────────────────────────────────────────────────────────
    int         vacuum_max_days{3650};
    std::string vacuum_max_size{"1TB"};
    int64_t     vacuum_max_size_bytes{};    // derived
    std::string vacuum_target_size{"800GB"};
    int64_t     vacuum_target_size_bytes{}; // derived
    int         vacuum_delete_batch_size{2500};

    // ── Background tasks ──────────────────────────────────────────────────────
    int task_diagnostics_interval{60};    // seconds
    int task_backlog_flush_interval{5};   // seconds
    int task_backlog_max_size{200};       // max entries before force-flush
    int task_vacuum_interval{120};        // seconds
    int task_vacuum_max_size{20};         // MB budget per incremental vacuum pass

    // ── Compression ───────────────────────────────────────────────────────────
    CompressionConfig compression;

    // ── Harvesters ────────────────────────────────────────────────────────────
    struct HarvesterDef {
        std::string type;
        std::string name;
        std::map<std::string, std::string> config;
    };
    std::vector<HarvesterDef> harvesters;

    // ── Migrations ────────────────────────────────────────────────────────────
    std::vector<Migration> migrations; // required – no default

    // ── Factory ───────────────────────────────────────────────────────────────
    static Config from_file(const std::filesystem::path& path);
};

} // namespace loglite
