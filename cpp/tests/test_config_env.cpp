#include <gtest/gtest.h>

#include "config.hpp"
#include "utils.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace loglite;

static fs::path write_temp_config(const std::string& yaml) {
    auto path = fs::temp_directory_path() / "test_loglite_config_env.yaml";
    std::ofstream{path} << yaml;
    return path;
}

static const char* kEnvConfig = R"yaml(
host: 127.0.0.1
port: 9999
log_table_name: TestLog
sqlite_dir: /tmp/loglite_test_db
migrations:
  - version: 1
    rollout:
      - "CREATE TABLE IF NOT EXISTS TestLog (id INTEGER PRIMARY KEY, timestamp TEXT NOT NULL, message TEXT NOT NULL)"
    rollback:
      - "DROP TABLE IF EXISTS TestLog"
)yaml";

TEST(ConfigEnvTest, EnvOverrideHost) {
    setenv("LOGLITE_host", "0.0.0.0", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_EQ(cfg.host, "0.0.0.0");
    unsetenv("LOGLITE_host");
}

TEST(ConfigEnvTest, EnvOverridePort) {
    setenv("LOGLITE_port", "12345", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_EQ(cfg.port, 12345);
    unsetenv("LOGLITE_port");
}

TEST(ConfigEnvTest, EnvOverrideDebug) {
    setenv("LOGLITE_debug", "true", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_TRUE(cfg.debug);
    unsetenv("LOGLITE_debug");
}

TEST(ConfigEnvTest, EnvOverrideDebugFalse) {
    setenv("LOGLITE_debug", "false", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_FALSE(cfg.debug);
    unsetenv("LOGLITE_debug");
}

TEST(ConfigEnvTest, EnvOverrideDebugOne) {
    setenv("LOGLITE_debug", "1", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_TRUE(cfg.debug);
    unsetenv("LOGLITE_debug");
}

TEST(ConfigEnvTest, EnvOverrideSSELimit) {
    setenv("LOGLITE_sse_limit", "500", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_EQ(cfg.sse_limit, 500);
    unsetenv("LOGLITE_sse_limit");
}

TEST(ConfigEnvTest, EnvOverrideAllowOrigin) {
    setenv("LOGLITE_allow_origin", "https://example.com", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_EQ(cfg.allow_origin, "https://example.com");
    unsetenv("LOGLITE_allow_origin");
}

TEST(ConfigEnvTest, EnvOverrideSqliteDir) {
    setenv("LOGLITE_sqlite_dir", "/tmp/custom_db", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_EQ(cfg.sqlite_dir, "/tmp/custom_db");
    unsetenv("LOGLITE_sqlite_dir");
}

TEST(ConfigEnvTest, EnvOverrideTaskInterval) {
    setenv("LOGLITE_task_diagnostics_interval", "30", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_EQ(cfg.task_diagnostics_interval, 30);
    unsetenv("LOGLITE_task_diagnostics_interval");
}

TEST(ConfigEnvTest, EnvOverridePoolSize) {
    setenv("LOGLITE_DB_POOL_SIZE", "6", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_EQ(cfg.db_pool_size, "6");
    EXPECT_EQ(cfg.resolve_pool_size(), 6u);
    unsetenv("LOGLITE_DB_POOL_SIZE");
}

TEST(ConfigEnvTest, MissingFileThrows) {
    EXPECT_THROW(Config::from_file("/nonexistent/loglite_config.yaml"), std::runtime_error);
}

TEST(ConfigEnvTest, VacuumSizeParseLargeValues) {
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_GT(cfg.vacuum_max_size_bytes, 0);
    EXPECT_LT(cfg.vacuum_target_size_bytes, cfg.vacuum_max_size_bytes);
}

TEST(ConfigEnvTest, HarvestersParsed) {
    std::string yaml = R"yaml(
host: 127.0.0.1
port: 9999
log_table_name: TestLog
sqlite_dir: /tmp/loglite_test_db
migrations:
  - version: 1
    rollout: ["SELECT 1"]
    rollback: ["SELECT 1"]
harvesters:
  - type: FileHarvester
    name: app-logs
    config:
      path: /var/log/app.log
  - type: loglite.harvesters.FileHarvester
    name: sys-logs
    config:
      path: /var/log/sys.log
)yaml";
    auto path = write_temp_config(yaml);
    auto cfg = Config::from_file(path);
    EXPECT_EQ(cfg.harvesters.size(), 2u);
    EXPECT_EQ(cfg.harvesters[0].type, "FileHarvester");
    EXPECT_EQ(cfg.harvesters[0].name, "app-logs");
    EXPECT_EQ(cfg.harvesters[0].config.at("path"), "/var/log/app.log");
}

TEST(ConfigEnvTest, MigrationWithoutVersionThrows) {
    std::string yaml = R"yaml(
host: 127.0.0.1
port: 9999
log_table_name: TestLog
sqlite_dir: /tmp/loglite_test_db
migrations:
  - rollout: ["SELECT 1"]
    rollback: ["SELECT 1"]
)yaml";
    auto path = write_temp_config(yaml);
    EXPECT_THROW(Config::from_file(path), std::runtime_error);
}

TEST(ConfigEnvTest, EmptyMigrationsThrows) {
    std::string yaml = R"yaml(
host: 127.0.0.1
port: 9999
log_table_name: TestLog
sqlite_dir: /tmp/loglite_test_db
migrations: []
)yaml";
    auto path = write_temp_config(yaml);
    EXPECT_THROW(Config::from_file(path), std::runtime_error);
}

TEST(ConfigEnvTest, HarvestersWithoutTypeThrows) {
    std::string yaml = R"yaml(
host: 127.0.0.1
port: 9999
log_table_name: TestLog
sqlite_dir: /tmp/loglite_test_db
migrations:
  - version: 1
    rollout: ["SELECT 1"]
    rollback: ["SELECT 1"]
harvesters:
  - name: bad-harvester
    config:
      path: /tmp
)yaml";
    auto path = write_temp_config(yaml);
    EXPECT_THROW(Config::from_file(path), std::runtime_error);
}

TEST(ConfigEnvTest, DebugEnvOverrideYes) {
    setenv("LOGLITE_debug", "yes", 1);
    auto path = write_temp_config(kEnvConfig);
    auto cfg = Config::from_file(path);
    EXPECT_TRUE(cfg.debug);
    unsetenv("LOGLITE_debug");
}
