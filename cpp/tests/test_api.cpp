#include <gtest/gtest.h>

#include "api.hpp"
#include "test_support.hpp"
#include "reader_database.hpp"
#include "writer_database.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <future>
#include <thread>

namespace fs = std::filesystem;
namespace asio = boost::asio;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;
using namespace loglite;
using namespace std::chrono_literals;

class ServerApiTest : public ::testing::Test {
   protected:
    void SetUp() override {
        tmp_ = directory_.path();
        config_path_ = tmp_ / "config.yaml";
        file_path_ = tmp_ / "input.log";
        std::ofstream{file_path_};
        asio::io_context io;
        tcp::acceptor port_picker{io, {tcp::v4(), 0}};
        port_ = port_picker.local_endpoint().port();
    }

    void TearDown() override {
        StopServer();
        if (run_.valid()) EXPECT_NO_THROW(run_.get());
    }

    void WriteConfig(std::string_view options = "", std::string_view extra_sql = "",
                     int flush_interval = 3600) {
        std::ofstream out{config_path_};
        out << fmt::format(R"yaml(host: 127.0.0.1
port: {}
sqlite_dir: {}
auto_rollout: true
task_backlog_flush_interval: {}
task_diagnostics_interval: 3600
task_vacuum_interval: 3600
{}
migrations:
  - version: 1
    rollout:
      - "CREATE TABLE Log (id INTEGER PRIMARY KEY, timestamp TEXT, message TEXT)"
      {}
    rollback:
      - "DROP TABLE Log"
)yaml",
                           port_, tmp_.string(), flush_interval, options, extra_sql);
    }

    void Start() {
        run_ = std::async(std::launch::async, [this] { RunServer(config_path_); });
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (run_.wait_for(0s) == std::future_status::ready) {
                run_.get();
                FAIL() << "Server exited before becoming ready";
            }
            try {
                if (Request("/health").result() == http::status::ok) return;
            } catch (const boost::system::system_error&) {
                std::this_thread::sleep_for(10ms);
            }
        }
        FAIL() << "Server did not become ready";
    }

    http::response<http::string_body> Request(std::string_view target) {
        asio::io_context io;
        tcp::socket socket{io};
        socket.connect({asio::ip::make_address("127.0.0.1"), port_});
        http::request<http::empty_body> req{http::verb::get, std::string{target}, 11};
        req.set(http::field::host, "127.0.0.1");
        req.keep_alive(false);
        http::write(socket, req);
        boost::beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(socket, buffer, res);
        return res;
    }

    void Stop() {
        StopServer();
        EXPECT_EQ(run_.wait_for(5s), std::future_status::ready);
        EXPECT_NO_THROW(run_.get());
    }

    size_t PersistedRows() {
        auto cfg = Config::from_file(config_path_);
        WriterDatabase db{cfg};
        db.Open();
        db.Initialize();
        ReaderDatabase reader{cfg, db.catalog()};
        reader.Open();
        return reader.Query({"*"}, {}, 100, 0).results.size();
    }

    test::TempDirectory directory_;
    fs::path tmp_;
    fs::path config_path_;
    fs::path file_path_;
    uint16_t port_{};
    std::future<void> run_;
};

TEST_F(ServerApiTest, ShutdownPersistsExternalIngestion) {
    WriteConfig();
    Start();
    PushToBacklog({{"message", "external"}});
    Stop();
    EXPECT_EQ(PersistedRows(), 1u);
    // Teardown must clear exported pointers before the objects are destroyed.
    StopServer();
    PushToBacklog({{"message", "after shutdown"}});
    EXPECT_EQ(PersistedRows(), 1u);
}

TEST_F(ServerApiTest, ShutdownPersistsHarvesterPartialLine) {
    WriteConfig(fmt::format(R"(harvesters:
  - type: FileHarvester
    name: input
    config:
      path: {}
)",
                            file_path_.string()),
                "", 1);
    Start();
    std::this_thread::sleep_for(600ms);  // let the harvester open the initial file at EOF
    {
        std::ofstream out{file_path_, std::ios::app};
        out << R"({"message":"complete"})" << '\n';
    }
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    bool harvested = false;
    while (std::chrono::steady_clock::now() < deadline) {
        auto body = nlohmann::json::parse(Request("/logs?fields=*&limit=10&offset=0").body());
        if (body["total"] == 1) {
            harvested = true;
            break;
        }
        std::this_thread::sleep_for(50ms);
    }
    ASSERT_TRUE(harvested);
    {
        std::ofstream out{file_path_, std::ios::app};
        out << R"({"message":"partial"})";  // no trailing newline
    }
    std::this_thread::sleep_for(700ms);  // let the harvester buffer the partial line
    Stop();
    EXPECT_EQ(PersistedRows(), 2u);
}

TEST_F(ServerApiTest, BackgroundFailurePropagatesThroughRunServer) {
    WriteConfig("task_backlog_max_size: 1",
                "- \"CREATE TRIGGER reject_log BEFORE INSERT ON Log BEGIN "
                "SELECT RAISE(ABORT, 'injected background failure'); END\"");
    Start();
    PushToBacklog({{"message", "reject"}});
    EXPECT_EQ(run_.wait_for(5s), std::future_status::ready);
    EXPECT_THROW(run_.get(), std::runtime_error);
    StopServer();
    PushToBacklog({{"message", "after failure"}});
    EXPECT_EQ(PersistedRows(), 0u);
}

TEST_F(ServerApiTest, FinalFlushFailurePropagatesThroughRunServer) {
    WriteConfig("",
                "- \"CREATE TRIGGER reject_log BEFORE INSERT ON Log BEGIN "
                "SELECT RAISE(ABORT, 'injected shutdown failure'); END\"");
    Start();
    PushToBacklog({{"message", "reject"}});
    StopServer();
    EXPECT_EQ(run_.wait_for(5s), std::future_status::ready);
    EXPECT_THROW(run_.get(), std::runtime_error);
    EXPECT_EQ(PersistedRows(), 0u);
}

TEST_F(ServerApiTest, LockedQueryReturns500AndRecoversAfterUnlock) {
    WriteConfig("db_pool_size: 1");
    Start();
    auto cfg = Config::from_file(config_path_);
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open(cfg.db_path.c_str(), &raw);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> peer{raw, sqlite3_close};
    ASSERT_EQ(rc, SQLITE_OK);
    Statement{peer.get(), "INSERT INTO Log (message) VALUES ('existing')"}.Step();
    ASSERT_EQ(Request("/logs?fields=*&limit=10&offset=0").result(), http::status::ok);
    Statement{peer.get(), "BEGIN EXCLUSIVE"}.Step();

    auto result = Request("/logs?fields=*&limit=10&offset=0");
    EXPECT_EQ(result.result(), http::status::internal_server_error);
    EXPECT_NE(result.body().find("sqlite3_step"), std::string::npos);
    auto stats = Request(
        "/stats?since=2024-01-01T00:00:00Z&until=2024-01-01T01:00:00Z"
        "&activity_stats_fields=*&database_stats_fields=*");
    EXPECT_EQ(stats.result(), http::status::internal_server_error);
    Statement{peer.get(), "ROLLBACK"}.Step();
    auto body = nlohmann::json::parse(Request("/logs?fields=*&limit=10&offset=0").body());
    EXPECT_EQ(body["total"], 1);
    Stop();
}

TEST_F(ServerApiTest, StartupFailureClearsExportedPointers) {
    WriteConfig();
    asio::io_context io;
    tcp::acceptor occupied{io};
    occupied.open(tcp::v4());
    occupied.set_option(tcp::acceptor::reuse_address{false});
    occupied.bind({asio::ip::make_address("127.0.0.1"), port_});
    occupied.listen();
    EXPECT_THROW(RunServer(config_path_), boost::system::system_error);
    StopServer();
    PushToBacklog({{"message", "after startup failure"}});
    occupied.close();
    Start();
    PushToBacklog({{"message", "after restart"}});
    Stop();
    EXPECT_EQ(PersistedRows(), 1u);
}

TEST_F(ServerApiTest, MigrationFailuresPropagateThroughApi) {
    WriteConfig("", "- \"INVALID SQL\"");
    EXPECT_THROW(Rollout(config_path_), std::runtime_error);
    EXPECT_THROW(RunServer(config_path_), std::runtime_error);
    StopServer();
    PushToBacklog({{"message", "after migration failure"}});
}
