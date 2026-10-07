#include "api.hpp"

#include "config.hpp"
#include "harvesters/base.hpp"
#include "harvesters/file.hpp"
#include "log.hpp"
#include "metrics.hpp"
#include "runtime.hpp"
#include "schedule.hpp"
#include "server.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace loglite {

using namespace std::chrono_literals;

namespace {

struct ActiveRun {
    uint64_t epoch{};
    std::shared_ptr<Submission::Gate> gate;
    Runtime* runtime{};
    Server* server{};
    std::mutex mu;
    std::condition_variable cv;
    bool producers_finished{true};
};

std::mutex g_mu;
std::shared_ptr<ActiveRun> g_run;
uint64_t g_next_epoch{1};

std::shared_ptr<ActiveRun> CurrentRun() {
    std::lock_guard lock{g_mu};
    return g_run;
}

std::vector<std::unique_ptr<harvesters::Harvester>> BuildNativeHarvesters(const Config& cfg,
                                                                          Submission submission) {
    std::vector<std::unique_ptr<harvesters::Harvester>> harvesters;
    for (const auto& hdef : cfg.harvesters) {
        if (hdef.type == "loglite.harvesters.FileHarvester" || hdef.type == "FileHarvester") {
            auto it = hdef.config.find("path");
            if (it == hdef.config.end()) {
                log::WARN("FileHarvester '{}': missing 'path' config", hdef.name);
                continue;
            }
            harvesters.push_back(
                std::make_unique<harvesters::FileHarvester>(hdef.name, it->second, submission));
        } else {
            log::WARN("Unknown harvester type '{}', skipping", hdef.type);
        }
    }
    return harvesters;
}

void WaitForProducers(const std::shared_ptr<ActiveRun>& run) {
    std::unique_lock lock{run->mu};
    run->cv.wait(lock, [&] { return run->producers_finished; });
}

}  // namespace

void RunServer(const std::filesystem::path& config_path, std::function<void()> on_ready,
               std::function<void()> on_stop) {
    auto cfg = Config::from_file(config_path);
    log::SetLevel(cfg.debug ? log::Level::kDebug : log::Level::kInfo);
    metrics::MetricsRegistry::Instance().Configure(cfg.task_diagnostics_interval * 1s);

    auto runtime = std::make_unique<Runtime>(std::move(cfg));
    auto run = std::make_shared<ActiveRun>();
    {
        std::lock_guard lock{g_mu};
        run->epoch = g_next_epoch++;
        run->gate = std::make_shared<Submission::Gate>();
        run->runtime = runtime.get();
        run->gate->submit = [rt = runtime.get()](nlohmann::json entry) {
            return rt->ingestion().Submit(std::move(entry)).admitted;
        };
        // The runtime's own gate is unused; producers bind to this run's gate.
        g_run = run;
    }

    auto native = BuildNativeHarvesters(runtime->config(), Submission{run->gate});
    Server server{*runtime};
    run->server = &server;
    std::exception_ptr failure;

    try {
        if (on_ready) on_ready();
        for (const auto& harvester : native) harvester->Start();
        log::INFO("loglite server starting on {}:{}", runtime->config().host,
                  runtime->config().port);
        server.Run();
    } catch (...) {
        failure = std::current_exception();
    }
    run->server = nullptr;

    if (on_stop) {
        {
            std::lock_guard lock{run->mu};
            run->producers_finished = false;
        }
        try {
            on_stop();
        } catch (...) {
            if (!failure) failure = std::current_exception();
        }
        WaitForProducers(run);
    }

    for (const auto& harvester : native) harvester->Stop();
    run->gate->Seal();
    runtime->ingestion().Seal();

    try {
        const int count =
            asio::co_spawn(
                runtime->write_strand(),
                [&]() -> asio::awaitable<int> { co_return runtime->Settle(); }, asio::use_future)
                .get();
        log::INFO("[Termination] flushed {} pending log(s)", count);
    } catch (const std::exception& e) {
        log::ERROR("[Termination] backlog flush failed: {}", e.what());
        if (!failure) failure = std::current_exception();
    }

    {
        std::lock_guard lock{g_mu};
        if (g_run == run) g_run.reset();
        run->runtime = nullptr;
    }
    runtime.reset();

    if (failure) std::rethrow_exception(failure);
}

void StopServer() {
    auto run = CurrentRun();
    if (run && run->server)
        run->server->Stop();
    else if (run && run->runtime)
        run->runtime->RequestStop();
}

void PushToBacklog(nlohmann::json entry) {
    auto run = CurrentRun();
    if (!run || !run->gate) return;
    run->gate->Push(std::move(entry));
}

Submission CurrentSubmission() {
    auto run = CurrentRun();
    return run && run->gate ? Submission{run->gate} : Submission{};
}

uint64_t CurrentEpoch() {
    auto run = CurrentRun();
    return run ? run->epoch : 0;
}

void NotifyProducersFinished(uint64_t epoch) {
    auto run = CurrentRun();
    if (!run || run->epoch != epoch) return;
    std::lock_guard lock{run->mu};
    run->producers_finished = true;
    run->cv.notify_all();
}

void Rollout(const std::filesystem::path& config_path, int start_version) {
    auto cfg = Config::from_file(config_path);
    cfg.auto_rollout = false;

    LogStore db{cfg};
    db.Open();
    if (!db.Rollout(start_version)) log::INFO("No pending migrations to apply.");
}

void Rollback(const std::filesystem::path& config_path, int version, bool force) {
    auto cfg = Config::from_file(config_path);
    cfg.auto_rollout = false;

    LogStore db{cfg};
    db.Open();
    db.Rollback(version, force);
}

}  // namespace loglite
