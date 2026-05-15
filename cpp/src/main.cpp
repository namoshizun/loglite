#include "api.hpp"
#include "log.hpp"
#include "version.hpp"

#include <CLI/CLI.hpp>
#include <format>
#include <string>

int main(int argc, char** argv) {
    CLI::App app{"loglite – lightweight log service"};
    app.set_version_flag("--version", std::string{loglite::kVersion});
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

    auto* rollout_cmd = migrate_cmd->add_subcommand("rollout", "Apply pending migrations");
    std::string rollout_config;
    int rollout_version{-1};
    rollout_cmd->add_option("-c,--config", rollout_config, "Path to config YAML")->required();
    rollout_cmd->add_option("-v,--version-id", rollout_version,
                            "Apply migrations with version > this");

    auto* rollback_cmd = migrate_cmd->add_subcommand("rollback", "Roll back a migration");
    std::string rollback_config;
    int rollback_version{};
    bool rollback_force{false};
    rollback_cmd->add_option("-c,--config", rollback_config, "Path to config YAML")->required();
    rollback_cmd->add_option("-v,--version-id", rollback_version, "Version to roll back")
        ->required();
    rollback_cmd->add_flag("-f,--force", rollback_force, "Skip confirmation prompt");

    CLI11_PARSE(app, argc, argv);

    // ── execute command ────────────────────────────────────────────────────────
    try {
        if (server_run->parsed()) {
            loglite::RunServer(server_config);
            return 0;
        }
        if (rollout_cmd->parsed()) {
            loglite::Rollout(rollout_config, rollout_version);
            return 0;
        }
        if (rollback_cmd->parsed()) {
            loglite::Rollback(rollback_config, rollback_version, rollback_force);
            return 0;
        }
    } catch (const std::exception& e) {
        loglite::log::error(std::format("Fatal: {}", e.what()));
        return 1;
    }

    return 0;
}
