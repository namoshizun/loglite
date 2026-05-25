#include "config.hpp"
#include "log.hpp"

#include <boost/describe.hpp>
#include <boost/mp11.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fmt/format.h>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <yaml-cpp/yaml.h>

extern "C" {
extern char** environ;
}

namespace loglite {

namespace {

namespace bd = boost::describe;
namespace mp11 = boost::mp11;

using StringMap = std::map<std::string, std::string>;

// ── Type traits ────────────────────────────────────────────────────────────

template <class>
inline constexpr bool always_false_v = false;

template <class T, class = void>
inline constexpr bool is_described_v = false;
template <class T>
inline constexpr bool is_described_v<T, std::void_t<bd::describe_members<T, bd::mod_public>>> =
    true;

template <class>
inline constexpr bool is_vector_v = false;
template <class T>
inline constexpr bool is_vector_v<std::vector<T>> = true;

template <class>
struct vector_element;
template <class T>
struct vector_element<std::vector<T>> {
    using type = T;
};
template <class T>
using vector_element_t = typename vector_element<T>::type;

/// A type representable as a single string — eligible for LOGLITE_* env-var overrides.
template <class T>
inline constexpr bool is_string_parseable_v =
    std::is_same_v<T, std::string> || std::is_same_v<T, bool> || std::is_integral_v<T> ||
    std::is_same_v<T, std::filesystem::path>;

// ── String → T  (for LOGLITE_* env vars) ───────────────────────────────────

template <class T>
T from_string(const std::string& s) {
    if constexpr (std::is_same_v<T, std::string>) {
        return s;
    } else if constexpr (std::is_same_v<T, bool>) {
        std::string lc = s;
        std::ranges::transform(lc, lc.begin(), ::tolower);
        return lc == "true" || lc == "1" || lc == "yes";
    } else if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(std::stoll(s));
    } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
        return std::filesystem::path(s);
    } else {
        static_assert(always_false_v<T>, "extend from_string<T> for this type");
    }
}

// ── Read LOGLITE_<UPPER_FIELD> from the process environment ────────────────

StringMap read_env_overrides() {
    static constexpr std::string_view prefix = "LOGLITE_";
    StringMap out;
    for (char** p = ::environ; *p; ++p) {
        std::string_view entry{*p};
        if (!entry.starts_with(prefix)) continue;

        auto eq = entry.find('=');
        if (eq == std::string_view::npos) continue;

        std::string key(entry.substr(prefix.size(), eq - prefix.size()));
        std::ranges::transform(key, key.begin(), ::tolower);
        out[key] = std::string(entry.substr(eq + 1));
    }
    return out;
}

// ── YAML::Node → T  (recursive, type-driven) ──────────────────────────────

template <class S>
    requires(is_described_v<S>)
void load_yaml_map(S& out, const YAML::Node& node);

/// Convert a single YAML node to a value of type T.
/// Handles scalars, std::map<string,string>, std::vector<E>, and Boost.Describe structs
/// recursively.
template <class T>
T from_yaml(const YAML::Node& node) {
    if constexpr (std::is_same_v<T, std::string>) {
        try {
            return node.as<std::string>();
        } catch (const YAML::BadConversion&) {
            // NOTE: a nasty hack... db_pool_size config in yaml
            // is parsed as int64_t, but we want to store it as a string
            // into config first...
            return std::to_string(node.as<int64_t>());
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        return node.as<bool>();
    } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
        return std::filesystem::path(node.as<std::string>());
    } else if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(node.as<int64_t>());
    } else if constexpr (std::is_same_v<T, StringMap>) {
        StringMap m;
        // Populate the map from the YAML node.
        if (node.IsMap()) {
            for (const auto& kv : node) {
                m[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
        }
        return m;
    } else if constexpr (is_vector_v<T>) {
        T v;
        if (node.IsSequence()) {
            for (const auto& e : node) {
                v.push_back(from_yaml<vector_element_t<T>>(e));
            }
        }
        return v;
    } else if constexpr (is_described_v<T>) {
        T s{};
        load_yaml_map(s, node);
        return s;
    } else {
        static_assert(always_false_v<T>, "extend from_yaml<T> for this type");
    }
}

/// Populate a Boost.Describe struct from a YAML map node, one member at a time.
template <class S>
    requires(is_described_v<S>)
void load_yaml_map(S& out, const YAML::Node& node) {
    if (!node || !node.IsMap()) return;

    mp11::mp_for_each<bd::describe_members<S, bd::mod_public>>([&](auto m) {
        using M = decltype(m);
        const YAML::Node child = node[std::string(M::name)];
        if (!child) return;
        using Field = std::remove_reference_t<decltype(out.*(M::pointer))>;
        out.*(M::pointer) = from_yaml<Field>(child);
    });
}

// ── Root Config loader  (YAML + env overrides for scalar fields) ───────────

/// Load every described member of Config.  For scalar (string-parseable) fields the environment
/// takes precedence over YAML; complex fields (maps, vectors, nested structs) come from YAML only.
void load_root_config(Config& cfg, const StringMap& env, const YAML::Node& yaml) {
    mp11::mp_for_each<bd::describe_members<Config, bd::mod_public>>([&](auto m) {
        using M = decltype(m);
        using Field = std::remove_reference_t<decltype(cfg.*(M::pointer))>;
        const std::string key{M::name};

        // Scalar fields: env takes precedence over YAML.
        if constexpr (is_string_parseable_v<Field>) {
            if (auto it = env.find(key); it != env.end()) {
                cfg.*(M::pointer) = from_string<Field>(it->second);
                return;  // env wins — skip YAML lookup
            }
        }

        // Fall through to YAML for all field types.
        const YAML::Node child = yaml[key];
        if (!child) return;
        cfg.*(M::pointer) = from_yaml<Field>(child);
    });
}

}  // namespace

unsigned Config::resolve_pool_size() const {
    const std::string_view raw = db_pool_size;
    const std::string_view t = strip_spaces(raw);
    if (t.empty()) {
        throw std::runtime_error("db_pool_size must be 'auto' or a positive integer");
    }

    std::string lc{t};
    std::ranges::transform(lc, lc.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lc == "auto") {
        return std::max(1u, std::thread::hardware_concurrency());
    }

    char* end = nullptr;
    const unsigned long n = std::strtoul(lc.c_str(), &end, 10);
    if (end == lc.c_str() || *end != '\0' || n == 0) {
        throw std::runtime_error(
            fmt::format("db_pool_size must be 'auto' or a positive integer, got '{}'", raw));
    }
    return static_cast<unsigned>(n);
}

Config Config::from_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path))
        throw std::runtime_error(fmt::format("Config file not found: {}", path.string()));

    // Load config
    YAML::Node yaml = YAML::LoadFile(path.string());
    auto env = read_env_overrides();
    Config cfg;

    load_root_config(cfg, env, yaml);

    // Run validation
    for (const auto& h : cfg.harvesters) {
        if (h.type.empty() || h.name.empty()) {
            throw std::runtime_error(
                "each 'harvesters' entry must include non-empty 'type' and 'name'");
        }
    }

    if (!yaml["migrations"] || !yaml["migrations"].IsSequence()) {
        throw std::runtime_error("'migrations' is required in config");
    }
    for (const auto& m : yaml["migrations"]) {
        if (!m["version"]) {
            throw std::runtime_error("each migration must have a 'version' key");
        }
    }
    if (cfg.migrations.empty()) {
        throw std::runtime_error("'migrations' list must not be empty");
    }
    if (cfg.task_diagnostics_interval < 30) {
        throw std::runtime_error("'task_diagnostics_interval' must be at least 30 seconds");
    }

    // Post init
    cfg.vacuum_max_size_bytes = parse_size_to_bytes(cfg.vacuum_max_size);
    cfg.vacuum_target_size_bytes = parse_size_to_bytes(cfg.vacuum_target_size);
    (void)cfg.resolve_pool_size();
    std::filesystem::create_directories(cfg.sqlite_dir);
    cfg.db_path = cfg.sqlite_dir / "logs.db";

    log::info(fmt::format("Config loaded from {}", path.string()));
    return cfg;
}

}  // namespace loglite
