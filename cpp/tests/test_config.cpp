#include <gtest/gtest.h>

#include "config.hpp"
#include "utils.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace loglite;

// Write a minimal config YAML to a temp file and return its path.
static fs::path write_temp_config(const std::string& yaml) {
  auto path = fs::temp_directory_path() / "test_loglite_config.yaml";
  std::ofstream{path} << yaml;
  return path;
}

static const char* kMinimalConfig = R"yaml(
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

TEST(ConfigTest, ParsesMinimalYaml) {
  auto path = write_temp_config(kMinimalConfig);
  auto cfg = Config::from_file(path);

  EXPECT_EQ(cfg.host, "127.0.0.1");
  EXPECT_EQ(cfg.port, 9999);
  EXPECT_EQ(cfg.log_table_name, "TestLog");
  EXPECT_EQ(cfg.migrations.size(), 1u);
  EXPECT_EQ(cfg.migrations[0].version, 1);
  EXPECT_FALSE(cfg.migrations[0].rollout.empty());
}

TEST(ConfigTest, DefaultValues) {
  auto path = write_temp_config(kMinimalConfig);
  auto cfg = Config::from_file(path);

  EXPECT_EQ(cfg.sse_limit, 1000);
  EXPECT_EQ(cfg.sse_debounce_ms, 500);
  EXPECT_EQ(cfg.vacuum_max_days, 3650);
  EXPECT_FALSE(cfg.debug);
  EXPECT_FALSE(cfg.auto_rollout);
  EXPECT_EQ(cfg.allow_origin, "*");
}

TEST(ConfigTest, DbPathDerived) {
  auto path = write_temp_config(kMinimalConfig);
  auto cfg = Config::from_file(path);

  EXPECT_EQ(cfg.db_path, cfg.sqlite_dir / "logs.db");
  EXPECT_TRUE(fs::exists(cfg.sqlite_dir));
}

TEST(ConfigTest, VacuumSizeParsed) {
  auto path = write_temp_config(kMinimalConfig);
  auto cfg = Config::from_file(path);

  EXPECT_GT(cfg.vacuum_max_size_bytes, 0);
  EXPECT_GT(cfg.vacuum_target_size_bytes, 0);
  EXPECT_LT(cfg.vacuum_target_size_bytes, cfg.vacuum_max_size_bytes);
}

TEST(ConfigTest, MissingMigrationsThrows) {
  auto path = write_temp_config("host: 127.0.0.1\n");
  EXPECT_THROW(Config::from_file(path), std::exception);
}

TEST(UtilsTest, ParseSizeToBytes) {
  EXPECT_EQ(parse_size_to_bytes("1KB"), 1024LL);
  EXPECT_EQ(parse_size_to_bytes("1MB"), 1024LL * 1024);
  EXPECT_EQ(parse_size_to_bytes("1GB"), 1024LL * 1024 * 1024);
  EXPECT_EQ(parse_size_to_bytes("2TB"), 2LL * 1024 * 1024 * 1024 * 1024);
  EXPECT_EQ(parse_size_to_bytes("500MB"), 500LL * 1024 * 1024);
}

TEST(UtilsTest, ParseSizeInvalidThrows) {
  EXPECT_THROW(parse_size_to_bytes("bad"), std::exception);
  EXPECT_THROW(parse_size_to_bytes(""), std::exception);
}
