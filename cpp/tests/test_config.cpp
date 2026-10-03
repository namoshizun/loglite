#include <gtest/gtest.h>

#include "test_support.hpp"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <optional>

using namespace loglite;

extern "C" {
extern char** environ;
}

namespace {

// Config tests must neither inherit nor leak the caller's LOGLITE_* overrides.
class ScopedEnvironment {
   public:
    ScopedEnvironment(std::initializer_list<std::pair<const char*, const char*>> overrides = {}) {
        for (char** entry = ::environ; *entry; ++entry) {
            std::string_view raw{*entry};
            if (raw.starts_with("LOGLITE_")) {
                const auto equals = raw.find('=');
                saved_.emplace_back(raw.substr(0, equals), raw.substr(equals + 1));
            }
        }
        for (const auto& [name, value] : saved_) ::unsetenv(name.c_str());
        for (const auto& [name, value] : overrides) {
            names_.emplace_back(name);
            ::setenv(name, value, 1);
        }
    }
    ~ScopedEnvironment() {
        for (const auto& name : names_) ::unsetenv(name.c_str());
        for (const auto& [name, value] : saved_) ::setenv(name.c_str(), value.c_str(), 1);
    }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

   private:
    std::vector<std::pair<std::string, std::string>> saved_;
    std::vector<std::string> names_;
};

constexpr std::string_view kMinimalConfig = R"yaml(host: 127.0.0.1
port: 9999
log_table_name: TestLog
migrations:
  - version: 1
    rollout: ["CREATE TABLE TestLog (id INTEGER PRIMARY KEY, timestamp TEXT NOT NULL, message TEXT NOT NULL)"]
    rollback: ["DROP TABLE TestLog"]
)yaml";

}  // namespace

class ConfigTest : public ::testing::Test {
   protected:
    // Write a minimal config YAML to an owned temporary file.
    Config LoadYaml(std::string_view yaml) {
        const auto path = directory_.path() / "config.yaml";
        {
            std::ofstream out{path};
            out << "sqlite_dir: " << directory_.path().string() << '\n' << yaml;
        }
        return Config::from_file(path);
    }
    Config Load(std::string_view extra = "") {
        return LoadYaml(std::string{kMinimalConfig} + std::string{extra});
    }

    ScopedEnvironment environment_;
    test::TempDirectory directory_;
};

TEST_F(ConfigTest, LoadsYamlDefaultsAndDerivedPaths) {
    const auto cfg = Load();
    EXPECT_EQ(cfg.host, "127.0.0.1");
    EXPECT_EQ(cfg.port, 9999);
    EXPECT_EQ(cfg.log_table_name, "TestLog");
    ASSERT_EQ(cfg.migrations.size(), 1u);
    EXPECT_EQ(cfg.migrations[0].version, 1);
    EXPECT_EQ(cfg.migrations[0].rollout.size(), 1u);
    EXPECT_EQ(cfg.migrations[0].rollback.size(), 1u);
    EXPECT_EQ(cfg.sse_limit, 1000);
    EXPECT_EQ(cfg.sse_debounce_ms, 500);
    EXPECT_EQ(cfg.vacuum_max_days, 3650);
    EXPECT_FALSE(cfg.debug);
    EXPECT_FALSE(cfg.auto_rollout);
    EXPECT_EQ(cfg.allow_origin, "*");
    EXPECT_EQ(cfg.db_pool_size, "2");
    EXPECT_EQ(cfg.resolve_pool_size(), 2u);
    EXPECT_EQ(cfg.db_path, directory_.path() / "logs.db");
    EXPECT_TRUE(std::filesystem::exists(cfg.sqlite_dir));
    EXPECT_EQ(cfg.vacuum_max_size_bytes, parse_size_to_bytes(cfg.vacuum_max_size));
    EXPECT_EQ(cfg.vacuum_target_size_bytes, parse_size_to_bytes(cfg.vacuum_target_size));
}

TEST_F(ConfigTest, PoolSizeParsingRejectsInvalidValues) {
    auto cfg = Load("db_pool_size: 4\n");
    EXPECT_EQ(cfg.db_pool_size, "4");
    EXPECT_EQ(cfg.resolve_pool_size(), 4u);
    cfg = Load("db_pool_size: auto\n");
    EXPECT_EQ(cfg.db_pool_size, "auto");
    EXPECT_GE(cfg.resolve_pool_size(), 1u);
    for (const auto* raw : {"0", "bogus", "-1", "+4", "12a", "99999999999999999999"}) {
        SCOPED_TRACE(raw);
        cfg.db_pool_size = raw;
        EXPECT_THROW((void)cfg.resolve_pool_size(), std::runtime_error);
        EXPECT_THROW(Load(fmt::format("db_pool_size: '{}'\n", raw)), std::runtime_error);
    }
}

TEST_F(ConfigTest, RejectsMalformedOrInvalidYaml) {
    struct Case {
        const char* name;
        const char* yaml;
    };
    const Case cases[]{
        {"missing migrations", "host: 127.0.0.1\n"},
        {"empty migrations", "migrations: []\n"},
        {"missing version", "migrations:\n  - rollout: [SELECT 1]\n"},
        {"missing harvester type", "migrations: [{version: 1}]\nharvesters: [{name: app}]\n"},
        {"missing harvester name",
         "migrations: [{version: 1}]\nharvesters: [{type: FileHarvester}]\n"},
        {"zero backlog", "migrations: [{version: 1}]\ntask_backlog_max_size: 0\n"},
        {"negative backlog", "migrations: [{version: 1}]\ntask_backlog_max_size: -1\n"},
        {"short diagnostics interval",
         "migrations: [{version: 1}]\ntask_diagnostics_interval: 29\n"},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.name);
        EXPECT_THROW(LoadYaml(c.yaml), std::runtime_error);
    }
    EXPECT_THROW(Config::from_file(directory_.path() / "missing.yaml"), std::runtime_error);
}

TEST_F(ConfigTest, DirectValidationChecksConfigurationInvariants) {
    Config valid;
    valid.migrations = {{.version = 1, .rollout = {"SELECT 1"}, .rollback = {}}};
    EXPECT_NO_THROW(valid.validate());
    struct Case {
        const char* name;
        std::function<void(Config&)> invalidate;
    };
    const Case cases[]{
        {"empty migrations", [](Config& c) { c.migrations.clear(); }},
        {"zero backlog", [](Config& c) { c.task_backlog_max_size = 0; }},
        {"short diagnostics interval", [](Config& c) { c.task_diagnostics_interval = 29; }},
        {"missing harvester type", [](Config& c) { c.harvesters.push_back({"", "app", {}}); }},
        {"missing harvester name",
         [](Config& c) { c.harvesters.push_back({"FileHarvester", "", {}}); }},
        {"invalid pool size", [](Config& c) { c.db_pool_size = "0"; }},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.name);
        auto cfg = valid;
        c.invalidate(cfg);
        EXPECT_THROW(cfg.validate(), std::runtime_error);
    }
}

TEST_F(ConfigTest, LoadsNestedHarvesterAndMigrationDefinitions) {
    const auto cfg = Load(R"yaml(harvesters:
  - type: FileHarvester
    name: app-logs
    config: {path: /var/log/app.log}
  - type: loglite.harvesters.FileHarvester
    name: sys-logs
    config: {path: /var/log/sys.log}
)yaml");
    ASSERT_EQ(cfg.harvesters.size(), 2u);
    EXPECT_EQ(cfg.harvesters[0].type, "FileHarvester");
    EXPECT_EQ(cfg.harvesters[0].name, "app-logs");
    EXPECT_EQ(cfg.harvesters[0].config.at("path"), "/var/log/app.log");
    EXPECT_EQ(cfg.harvesters[1].type, "loglite.harvesters.FileHarvester");
    EXPECT_EQ(cfg.harvesters[1].name, "sys-logs");
    EXPECT_EQ(cfg.harvesters[1].config.at("path"), "/var/log/sys.log");
}

TEST_F(ConfigTest, EnvironmentScalarOverridesTakePrecedenceOverYaml) {
    const auto custom_dir = directory_.path() / "override";
    const ScopedEnvironment overrides{
        {"LOGLITE_host", "0.0.0.0"},
        {"LOGLITE_port", "12345"},
        {"LOGLITE_sse_limit", "500"},
        {"LOGLITE_allow_origin", "https://example.com"},
        {"LOGLITE_sqlite_dir", custom_dir.c_str()},
        {"LOGLITE_task_diagnostics_interval", "30"},
        {"LOGLITE_DB_POOL_SIZE", "6"},
    };
    const auto cfg = Load();
    EXPECT_EQ(cfg.host, "0.0.0.0");
    EXPECT_EQ(cfg.port, 12345);
    EXPECT_EQ(cfg.sse_limit, 500);
    EXPECT_EQ(cfg.allow_origin, "https://example.com");
    EXPECT_EQ(cfg.sqlite_dir, custom_dir);
    EXPECT_EQ(cfg.db_path, custom_dir / "logs.db");
    EXPECT_EQ(cfg.task_diagnostics_interval, 30);
    EXPECT_EQ(cfg.db_pool_size, "6");
    EXPECT_EQ(cfg.resolve_pool_size(), 6u);
}

TEST_F(ConfigTest, EnvironmentBooleansAcceptDocumentedAliases) {
    for (const auto* raw : {"true", "1", "yes", "TRUE", "YeS", "false"}) {
        SCOPED_TRACE(raw);
        const ScopedEnvironment override{{"LOGLITE_debug", raw}};
        EXPECT_EQ(Load().debug, std::string_view{raw} != "false");
    }
}
