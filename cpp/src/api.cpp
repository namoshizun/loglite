#include "api.hpp"

#include "config.hpp"
#include "log_store.hpp"
#include "context.hpp"
#include "harvesters/base.hpp"
#include "harvesters/file.hpp"
#include "log.hpp"
#include "metrics.hpp"
#include "server.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace loglite {

using namespace std::chrono_literals;

namespace {

// Module-level state set during RunServer so PushToBacklog / StopServer work.
// Access and teardown are synchronized; only one server runs per process.
Backlog* g_backlog{nullptr};
Server* g_server{nullptr};

void FlushPendingBacklog(ServerContext& ctx) {
    // Queue behind in-flight work and keep all access to the writer on its strand.
    const int count =
        asio::co_spawn(
            ctx.write_strand, [&ctx]() -> asio::awaitable<int> { co_return ctx.FlushBacklog(); },
            asio::use_future)
            .get();
    log::INFO("[Termination] flushed {} pending log(s)", count);
}

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

    LogStore db_write{cfg};
    db_write.Open();
    db_write.Initialize();

    LogReaderPool db_read(db_write, cfg.resolve_pool_size());

    // Init server context
    Backlog backlog{static_cast<size_t>(cfg.task_backlog_max_size)};
    LogNotifier notifier{static_cast<size_t>(cfg.sse_limit)};

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

    Server server{ctx};
    g_backlog = &backlog;
    g_server = &server;

    auto native = BuildNativeHarvesters(cfg, backlog);

    std::exception_ptr failure;
    try {
        for (const auto& harvester : native) harvester->Start();
        log::INFO("loglite server starting on {}:{}", cfg.host, cfg.port);
        server.Run();
    } catch (...) {
        failure = std::current_exception();
    }

    // Teardown
    g_server = nullptr;
    g_backlog = nullptr;

    for (const auto& harvester : native) harvester->Stop();

    // All producers have stopped, including harvesters that emit a partial line in Stop().
    try {
        FlushPendingBacklog(ctx);
    } catch (const std::exception& e) {
        log::ERROR("[Termination] backlog flush failed: {}", e.what());
        if (!failure) {
            failure = std::current_exception();
        }
    }

    db_write_pool.join();
    db_read_pool.join();

    db_read.Close();
    db_write.Close();

    if (failure) {
        std::rethrow_exception(failure);
    }
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

    LogStore db{cfg};
    db.Open();
    if (!db.Rollout(start_version)) {
        log::INFO("No pending migrations to apply.");
    }
}

void Rollback(const std::filesystem::path& config_path, int version, bool force) {
    auto cfg = Config::from_file(config_path);
    cfg.auto_rollout = false;

    LogStore db{cfg};
    db.Open();
    db.Rollback(version, force);
}

}  // namespace loglite
