#include "api.hpp"

#include "config.hpp"
#include "database.hpp"
#include "globals.hpp"
#include "harvesters/base.hpp"
#include "harvesters/file.hpp"
#include "log.hpp"
#include "migrations.hpp"
#include "server.hpp"

#include <atomic>
#include <format>
#include <memory>
#include <thread>
#include <vector>

namespace loglite {

namespace {

// Module-level state set during RunServer so PushToBacklog / StopServer work.
// Protected by the guarantee that only one server runs per process.
Backlog* g_backlog{nullptr};
Server* g_server{nullptr};

std::vector<std::unique_ptr<harvesters::Harvester>> BuildNativeHarvesters(const Config& cfg,
                                                                          Backlog& backlog) {
    std::vector<std::unique_ptr<harvesters::Harvester>> harvesters;
    for (const auto& hdef : cfg.harvesters) {
        if (hdef.type == "loglite.harvesters.FileHarvester" || hdef.type == "FileHarvester") {
            auto it = hdef.config.find("path");
            if (it == hdef.config.end()) {
                log::warn(std::format("FileHarvester '{}': missing 'path' config", hdef.name));
                continue;
            }
            harvesters.push_back(
                std::make_unique<harvesters::FileHarvester>(hdef.name, it->second, backlog));
        } else {
            log::warn(std::format("Unknown harvester type '{}', skipping", hdef.type));
        }
    }
    return harvesters;
}

}  // namespace

void RunServer(const std::filesystem::path& config_path, unsigned int thread_count) {
    // Load config and init database
    auto cfg = Config::from_file(config_path);
    Database db{cfg};
    db.Open();
    db.Initialize();

    // Init context
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

    g_backlog = &backlog;

    // Build & start harvesters
    auto native = BuildNativeHarvesters(cfg, backlog);
    for (const auto& harvester : native) {
        harvester->Start();
    }

    // Start server
    auto effective_threads =
        thread_count > 0 ? thread_count : std::max(1u, std::thread::hardware_concurrency());
    Server server{ctx, effective_threads};
    g_server = &server;

    log::info(std::format("loglite server starting on {}:{}", cfg.host, cfg.port));
    server.Run();

    // Teardown
    g_server = nullptr;
    g_backlog = nullptr;

    for (const auto& harvester : native) {
        harvester->Stop();
    }
    db.Close();
}

void StopServer() {
    if (g_server) g_server->Stop();
}

void PushToBacklog(nlohmann::json entry) {
    if (g_backlog) g_backlog->Add(std::move(entry));
}

// ── Migrations ────────────────────────────────────────────────────────────────

void Rollout(const std::filesystem::path& config_path, int start_version) {
    auto cfg = Config::from_file(config_path);
    cfg.auto_rollout = false;

    Database db{cfg};
    db.Open();
    db.CreateInternalTables();
    MigrationManager mgr{db, cfg.migrations};
    if (!mgr.ApplyPendingMigrations(start_version)) {
        log::info("No pending migrations to apply.");
    }
}

void Rollback(const std::filesystem::path& config_path, int version, bool force) {
    auto cfg = Config::from_file(config_path);
    cfg.auto_rollout = false;

    Database db{cfg};
    db.Open();
    db.CreateInternalTables();
    MigrationManager mgr{db, cfg.migrations};
    mgr.RollbackMigration(version, force);
}

}  // namespace loglite
