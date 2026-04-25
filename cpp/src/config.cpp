#include "config.hpp"
#include "log.hpp"

#include <cstdlib>
#include <format>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

// POSIX environment block; declared at file scope to avoid internal-linkage warning.
extern "C" { extern char** environ; }

namespace loglite {

namespace {

// Read LOGLITE_<UPPER_FIELD> env vars and return a map of lowercased field names.
std::map<std::string, std::string> read_env_overrides() {
    static constexpr std::string_view prefix = "LOGLITE_";
    std::map<std::string, std::string> out;

    for (char** p = ::environ; *p; ++p) {
        std::string_view entry{*p};
        if (!entry.starts_with(prefix)) continue;
        auto eq = entry.find('=');
        if (eq == std::string_view::npos) continue;
        std::string key = std::string(entry.substr(prefix.size(), eq - prefix.size()));
        std::ranges::transform(key, key.begin(), ::tolower);
        out[key] = std::string(entry.substr(eq + 1));
    }
    return out;
}

// Helper: return env override if present, else YAML node value, else nullopt.
std::optional<std::string> get(
    const std::map<std::string, std::string>& env,
    const YAML::Node&                         yaml,
    std::string_view                          field)
{
    std::string key{field};
    if (auto it = env.find(key); it != env.end()) return it->second;
    if (yaml[key]) return yaml[key].as<std::string>();
    return std::nullopt;
}

// Read a bool from env/yaml.
std::optional<bool> get_bool(
    const std::map<std::string, std::string>& env,
    const YAML::Node&                         yaml,
    std::string_view                          field)
{
    if (auto v = get(env, yaml, field)) {
        std::string lower = *v;
        std::ranges::transform(lower, lower.begin(), ::tolower);
        return lower == "true" || lower == "1" || lower == "yes";
    }
    return std::nullopt;
}

// Read an int from env/yaml.
std::optional<int> get_int(
    const std::map<std::string, std::string>& env,
    const YAML::Node&                         yaml,
    std::string_view                          field)
{
    if (auto v = get(env, yaml, field)) return std::stoi(*v);
    return std::nullopt;
}

} // namespace

Config Config::from_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path))
        throw std::runtime_error(std::format("Config file not found: {}", path.string()));

    YAML::Node yaml = YAML::LoadFile(path.string());
    auto env        = read_env_overrides();
    Config cfg;

    // ── Scalars ───────────────────────────────────────────────────────────────

    if (auto v = get(env, yaml, "host"))             cfg.host         = *v;
    if (auto v = get_int(env, yaml, "port"))         cfg.port         = static_cast<uint16_t>(*v);
    if (auto v = get_bool(env, yaml, "debug"))       cfg.debug        = *v;
    if (auto v = get(env, yaml, "allow_origin"))     cfg.allow_origin = *v;
    if (auto v = get_bool(env, yaml, "auto_rollout"))cfg.auto_rollout = *v;
    if (auto v = get(env, yaml, "log_table_name"))   cfg.log_table_name        = *v;
    if (auto v = get(env, yaml, "log_timestamp_field")) cfg.log_timestamp_field = *v;
    if (auto v = get(env, yaml, "sqlite_dir"))       cfg.sqlite_dir   = *v;
    if (auto v = get_int(env, yaml, "sse_limit"))         cfg.sse_limit         = *v;
    if (auto v = get_int(env, yaml, "sse_debounce_ms"))   cfg.sse_debounce_ms   = *v;
    if (auto v = get_int(env, yaml, "vacuum_max_days"))   cfg.vacuum_max_days   = *v;
    if (auto v = get_int(env, yaml, "vacuum_delete_batch_size")) cfg.vacuum_delete_batch_size = *v;
    if (auto v = get_int(env, yaml, "task_diagnostics_interval"))  cfg.task_diagnostics_interval  = *v;
    if (auto v = get_int(env, yaml, "task_backlog_flush_interval")) cfg.task_backlog_flush_interval = *v;
    if (auto v = get_int(env, yaml, "task_backlog_max_size"))      cfg.task_backlog_max_size      = *v;
    if (auto v = get_int(env, yaml, "task_vacuum_interval"))       cfg.task_vacuum_interval       = *v;
    if (auto v = get_int(env, yaml, "task_vacuum_max_size"))       cfg.task_vacuum_max_size       = *v;

    // ── Size fields (string with suffix, e.g. "500MB") ───────────────────────

    if (auto v = get(env, yaml, "vacuum_max_size")) {
        cfg.vacuum_max_size       = *v;
    }
    cfg.vacuum_max_size_bytes = parse_size_to_bytes(cfg.vacuum_max_size);

    if (auto v = get(env, yaml, "vacuum_target_size")) {
        cfg.vacuum_target_size    = *v;
    }
    cfg.vacuum_target_size_bytes = parse_size_to_bytes(cfg.vacuum_target_size);

    // ── SQLite PRAGMAs ────────────────────────────────────────────────────────

    if (yaml["sqlite_params"] && yaml["sqlite_params"].IsMap()) {
        for (const auto& kv : yaml["sqlite_params"]) {
            cfg.sqlite_params[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }

    // ── Compression ───────────────────────────────────────────────────────────

    if (yaml["compression"]) {
        auto c = yaml["compression"];
        if (c["enabled"]) cfg.compression.enabled = c["enabled"].as<bool>();
        if (c["columns"] && c["columns"].IsSequence()) {
            for (const auto& col : c["columns"])
                cfg.compression.columns.push_back(col.as<std::string>());
        }
    }

    // ── Harvesters ────────────────────────────────────────────────────────────

    if (yaml["harvesters"] && yaml["harvesters"].IsSequence()) {
        for (const auto& h : yaml["harvesters"]) {
            Config::HarvesterDef def;
            def.type = h["type"].as<std::string>();
            def.name = h["name"].as<std::string>();
            if (h["config"] && h["config"].IsMap()) {
                for (const auto& kv : h["config"])
                    def.config[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
            cfg.harvesters.push_back(std::move(def));
        }
    }

    // ── Migrations (required) ─────────────────────────────────────────────────

    if (!yaml["migrations"] || !yaml["migrations"].IsSequence())
        throw std::runtime_error("'migrations' is required in config");

    for (const auto& m : yaml["migrations"]) {
        Migration mg;
        mg.version = m["version"].as<int>();
        if (m["rollout"] && m["rollout"].IsSequence()) {
            for (const auto& stmt : m["rollout"])
                mg.rollout.push_back(stmt.as<std::string>());
        }
        if (m["rollback"] && m["rollback"].IsSequence()) {
            for (const auto& stmt : m["rollback"])
                mg.rollback.push_back(stmt.as<std::string>());
        }
        cfg.migrations.push_back(std::move(mg));
    }

    if (cfg.migrations.empty())
        throw std::runtime_error("'migrations' list must not be empty");

    // ── Derived paths ─────────────────────────────────────────────────────────

    std::filesystem::create_directories(cfg.sqlite_dir);
    cfg.db_path = cfg.sqlite_dir / "logs.db";

    log::info(std::format("Config loaded from {}", path.string()));
    return cfg;
}

} // namespace loglite
