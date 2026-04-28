#include "config.hpp"
#include "database.hpp"
#include "globals.hpp"
#include "harvesters/file.hpp"
#include "log.hpp"
#include "migrations.hpp"
#include "server.hpp"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace loglite;

// -- Helper
template <class F>
    requires std::is_invocable_r_v<int, F, MigrationManager&>
static int with_migrations(const fs::path& config_path, F&& f) {
    auto cfg = Config::from_file(config_path);
    cfg.auto_rollout = false;

    Database db{cfg};
    db.Open();
    db.CreateInternalTables();
    MigrationManager mgr{db, cfg.migrations};
    return std::forward<F>(f)(mgr);
}

// ── Server command ─────────────────────────────────────────────────────────────

static int cmd_server_run(const fs::path& config_path) {
    auto cfg = Config::from_file(config_path);

    Database db{cfg};
    db.Open();
    db.Initialize();

    Backlog backlog{static_cast<size_t>(cfg.task_backlog_max_size)};
    LogNotifier notifier;
    StatsTracker ingest_stats, query_stats;

    asio::thread_pool db_ops_pool{1u};
    ServerContext ctx{
        cfg,
        db,
        backlog,
        notifier,
        ingest_stats,
        query_stats,
        asio::make_strand(db_ops_pool.get_executor()),
    };

    // ── Start harvesters ──────────────────────────────────────────────────────
    std::vector<std::unique_ptr<harvesters::Harvester>> active_harvesters;
    for (const auto& hdef : cfg.harvesters) {
        if (hdef.type == "loglite.harvesters.FileHarvester" || hdef.type == "FileHarvester") {
            auto it = hdef.config.find("path");
            if (it == hdef.config.end()) {
                log::warn(std::format("FileHarvester '{}': missing 'path' config", hdef.name));
                continue;
            }
            auto h = std::make_unique<harvesters::FileHarvester>(hdef.name, it->second, backlog);
            h->Start();
            active_harvesters.push_back(std::move(h));
        } else {
            log::warn(std::format("Unknown harvester type '{}', skipping", hdef.type));
        }
    }

    auto thread_count = std::max(1u, std::thread::hardware_concurrency());
    Server server{ctx, thread_count};
    log::info(std::format("loglite server starting on {}:{}", cfg.host, cfg.port));
    server.Run();

    for (auto& h : active_harvesters) h->Stop();
    db.Close();
    return 0;
}

// ── Migration commands ─────────────────────────────────────────────────────────
static int cmd_migrate_rollout(const fs::path& config_path, int start_version) {
    return with_migrations(config_path, [start_version](MigrationManager& mgr) -> int {
        bool applied = mgr.ApplyPendingMigrations(start_version);
        if (!applied) log::info("No pending migrations to apply.");
        return 0;
    });
}

static int cmd_migrate_rollback(const fs::path& config_path, int version, bool force) {
    return with_migrations(config_path, [version, force](MigrationManager& mgr) -> int {
        mgr.RollbackMigration(version, force);
        return 0;
    });
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    CLI::App app{"loglite – lightweight log service"};
    app.require_subcommand(1);

    // ── server ────────────────────────────────────────────────────────────────
    auto* server_cmd = app.add_subcommand("server", "Server commands");
    server_cmd->require_subcommand(1);

    auto* server_run = server_cmd->add_subcommand("run", "Start the server");
    std::string server_config;
    server_run->add_option("-c,--config", server_config, "Path to config YAML")->required();

    // ── migrate ───────────────────────────────────────────────────────────────
    auto* migrate_cmd = app.add_subcommand("migrate", "Migration commands");
    migrate_cmd->require_subcommand(1);

    // [1] rollout
    auto* rollout_cmd = migrate_cmd->add_subcommand("rollout", "Apply pending migrations");
    std::string rollout_config;
    int rollout_version{-1};
    rollout_cmd->add_option("-c,--config", rollout_config, "Path to config YAML")->required();
    rollout_cmd->add_option("-v,--version-id", rollout_version,
                            "Apply migrations with version > this");

    // [2] rollback
    auto* rollback_cmd = migrate_cmd->add_subcommand("rollback", "Roll back a migration");
    std::string rollback_config;
    int rollback_version{};
    bool rollback_force{false};
    rollback_cmd->add_option("-c,--config", rollback_config, "Path to config YAML")->required();
    rollback_cmd->add_option("-v,--version-id", rollback_version, "Version to roll back")
        ->required();
    rollback_cmd->add_flag("-f,--force", rollback_force, "Skip confirmation prompt");

    CLI11_PARSE(app, argc, argv);

    // Execute
    try {
        if (server_run->parsed()) return cmd_server_run(server_config);

        if (rollout_cmd->parsed()) return cmd_migrate_rollout(rollout_config, rollout_version);

        if (rollback_cmd->parsed())
            return cmd_migrate_rollback(rollback_config, rollback_version, rollback_force);
    } catch (const std::exception& e) {
        log::error(std::format("Fatal: {}", e.what()));
        return 1;
    }

    return 0;
}
