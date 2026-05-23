#include "config.hpp"
#include "http.hpp"
#include "schema.hpp"
#include "sender.hpp"

#include <CLI/CLI.hpp>

#include <chrono>
#include <csignal>
#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <ranges>

namespace asio = boost::asio;

using namespace std::chrono_literals;

namespace {

[[nodiscard]] bool ValidateConfig(const burn::Config& cfg) {
    if (cfg.concurrency == 0) {
        std::cerr << "error: --concurrency must be > 0\n";
        return false;
    }
    if (cfg.qps <= 0.0) {
        std::cerr << "error: --qps must be > 0\n";
        return false;
    }
    if (cfg.info_ratio < 0.0 || cfg.info_ratio > 1.0) {
        std::cerr << "error: --info-ratio must be in [0, 1]\n";
        return false;
    }
    return true;
}

void RequestStop(const std::shared_ptr<burn::RunControl>& ctrl,
                 asio::steady_timer& duration_timer) {
    ctrl->stop.store(true, std::memory_order_release);
    duration_timer.cancel();
}

}  // namespace

int main(int argc, char** argv) {
    burn::Config cfg;

    CLI::App app{"loglite burn – HTTP ingest load generator"};
    std::string endpoint_url = "http://localhost:7788";
    app.add_option("--endpoint", endpoint_url, "Loglite base URL (http://host:port)");
    app.add_option("--concurrency", cfg.concurrency, "Concurrent senders")->required();
    app.add_option("--qps", cfg.qps, "Total requests per second")->required();
    app.add_option("--message-size", cfg.message_size_mean, "Mean log message field length");
    app.add_option("--info-ratio", cfg.info_ratio, "Fraction of logs at INFO level");
    app.add_option("--duration", cfg.duration_sec, "Run duration in seconds");

    CLI11_PARSE(app, argc, argv);

    if (!ValidateConfig(cfg)) {
        return 1;
    }

    try {
        cfg.endpoint = burn::ParseEndpoint(endpoint_url);
    } catch (const std::exception& e) {
        std::cerr << fmt::format("error: {}\n", e.what());
        return 1;
    }

    cfg.ComputePerSenderQps();

    burn::SchemaPlan plan;
    try {
        plan = burn::FetchSchemaPlan(cfg.endpoint);
    } catch (const std::exception& e) {
        std::cerr << fmt::format("error: schema fetch failed: {}\n", e.what());
        return 1;
    }

    // Init control
    asio::io_context ioc;
    auto ctrl = std::make_shared<burn::RunControl>(ioc);
    ctrl->senders_left.store(cfg.concurrency, std::memory_order_relaxed);

    const auto started = std::chrono::steady_clock::now();

    // Set the stop timer
    asio::steady_timer duration_timer{ioc};
    duration_timer.expires_after(std::chrono::seconds{cfg.duration_sec});
    duration_timer.async_wait([ctrl](const boost::system::error_code& ec) {
        if (!ec) {
            ctrl->stop.store(true, std::memory_order_release);
        }
    });

    // Set the exit signal handling
    asio::signal_set signals{ioc, SIGTERM, SIGINT};
    signals.async_wait([&duration_timer, ctrl](const boost::system::error_code& ec, int) {
        if (!ec) {
            RequestStop(ctrl, duration_timer);
        }
    });

    // Party begins!
    for (const unsigned i : std::views::iota(0u, cfg.concurrency)) {
        asio::co_spawn(ioc, burn::SenderLoop(i, cfg, plan, ctrl), [](std::exception_ptr eptr) {
            if (!eptr) {
                return;
            }
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                std::cerr << fmt::format("sender error: {}\n", e.what());
            }
        });
    }

    ioc.run();

    // Print stats
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const uint64_t ok = ctrl->stats->ok.load(std::memory_order_relaxed);
    const uint64_t fail = ctrl->stats->fail.load(std::memory_order_relaxed);
    const double effective_qps = elapsed > 0.0 ? static_cast<double>(ok) / elapsed : 0.0;

    std::cerr << fmt::format("burn finished: {:.1f}s  ok={}  fail={}  effective_qps={:.1f}\n",
                             elapsed, ok, fail, effective_qps);

    return fail > 0 ? 1 : 0;
}
