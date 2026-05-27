#include "api.hpp"

#include "config.hpp"
#include "writer_database.hpp"
#include "context.hpp"
#include "harvesters/base.hpp"
#include "harvesters/file.hpp"
#include "log.hpp"
#include "metrics.hpp"
#include "migrations.hpp"
#include "server.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace loglite {

using namespace std::chrono_literals;

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
                log::WARN("FileHarvester '{}': missing 'path' config", hdef.name);
                continue;
            }
            harvesters.push_back(
                std::make_unique<harvesters::FileHarvester>(hdef.name, it->second, backlog));
        } else {
            log::WARN("Unknown harvester type '{}', skipping", hdef.type);
        }
    }
    return harvesters;
}

}  // namespace

void RunServer(const std::filesystem::path& config_path) {
    // Load config and init database
    auto cfg = Config::from_file(config_path);
    log::SetLevel(cfg.debug ? log::Level::kDebug : log::Level::kInfo);
    metrics::MetricsRegistry::Instance().Configure(cfg.task_diagnostics_interval * 1s);
    WriterDatabase db_write{cfg};
    db_write.Open();
    db_write.Initialize();

    ReadDatabasePool db_read(cfg, db_write.catalog(), cfg.resolve_pool_size());

    // Init server context
    Backlog backlog{static_cast<size_t>(cfg.task_backlog_max_size)};
    LogNotifier notifier;
    notifier.Notify(db_write.GetMaxLogId());

    asio::thread_pool db_write_pool{1u};
    asio::thread_pool db_read_pool{cfg.resolve_pool_size()};
    const auto server_started_at = std::chrono::steady_clock::now();
    ServerContext ctx{cfg,
                      db_write,
                      db_read,
                      backlog,
                      notifier,
                      asio::make_strand(db_write_pool.get_executor()),
                      db_read_pool.get_executor(),
                      server_started_at};

    g_backlog = &backlog;

    // Start harvesters
    auto native = BuildNativeHarvesters(cfg, backlog);
    for (const auto& harvester : native) {
        harvester->Start();
    }

    // Run server
    Server server{ctx};
    g_server = &server;

    log::INFO("loglite server starting on {}:{}", cfg.host, cfg.port);
    server.Run();

    // Teardown
    ctx.RequestStop();
    g_server = nullptr;
    g_backlog = nullptr;

    for (const auto& harvester : native) {
        harvester->Stop();
    }

    db_write_pool.stop();
    db_read_pool.stop();
    db_write_pool.join();
    db_read_pool.join();

    db_read.Close();
    db_write.Close();
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

    WriterDatabase db{cfg};
    db.Open();
    db.CreateInternalTables();
    MigrationManager mgr{db, cfg.migrations};
    if (!mgr.ApplyPendingMigrations(start_version)) {
        log::INFO("No pending migrations to apply.");
    }
}

void Rollback(const std::filesystem::path& config_path, int version, bool force) {
    auto cfg = Config::from_file(config_path);
    cfg.auto_rollout = false;

    WriterDatabase db{cfg};
    db.Open();
    db.CreateInternalTables();
    MigrationManager mgr{db, cfg.migrations};
    mgr.RollbackMigration(version, force);
}

}  // namespace loglite
