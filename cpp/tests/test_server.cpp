#include <gtest/gtest.h>

#include "config.hpp"
#include "database.hpp"
#include "globals.hpp"
#include "metrics.hpp"
#include "server.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
using namespace loglite;

// ── HTTP client helper ──────────────────────────────────────────────────────

static http::response<http::string_body> http_req(const std::string& host, uint16_t port,
                                                  http::verb method, std::string_view target,
                                                  std::string_view body = "",
                                                  std::string_view content_type = "") {
    asio::io_context ioc;
    tcp::socket socket{ioc};
    tcp::resolver resolver{ioc};
    auto endpoints = resolver.resolve(host, std::to_string(port));
    asio::connect(socket, endpoints);

    http::request<http::string_body> req{method, std::string(target), 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "loglite-test");
    if (!body.empty()) {
        req.body() = std::string(body);
        if (!content_type.empty()) req.set(http::field::content_type, content_type);
        req.prepare_payload();
    }
    http::write(socket, req);

    beast::flat_buffer buf;
    http::response<http::string_body> res;
    http::read(socket, buf, res);

    beast::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
    return res;
}

// ── Fixture ──────────────────────────────────────────────────────────────────

class ServerTest : public ::testing::Test {
   protected:
    static void SetUpTestSuite() {}  // one-time setup if needed

    void SetUp() override {
        metrics::MetricsRegistry::Instance().ResetForTest();

        tmp_ = fs::temp_directory_path() / "loglite_server_test";
        fs::remove_all(tmp_);
        fs::create_directories(tmp_);

        cfg_.sqlite_dir = tmp_;
        cfg_.db_path = tmp_ / "logs.db";
        cfg_.log_table_name = "TestLog";
        cfg_.log_timestamp_field = "timestamp";
        cfg_.auto_rollout = true;
        cfg_.compression = {false, {}};
        cfg_.host = "127.0.0.1";
        cfg_.port = 17788;
        cfg_.allow_origin = "*";
        cfg_.task_diagnostics_interval = 3600;
        cfg_.task_backlog_flush_interval = 3600;
        cfg_.task_vacuum_interval = 3600;

        Migration m;
        m.version = 1;
        m.rollout = {
            "CREATE TABLE IF NOT EXISTS TestLog ("
            "  id        INTEGER PRIMARY KEY,"
            "  timestamp TEXT    NOT NULL,"
            "  message   TEXT    NOT NULL,"
            "  level     TEXT    NOT NULL,"
            "  service   TEXT"
            ")"};
        m.rollback = {"DROP TABLE IF EXISTS TestLog"};
        cfg_.migrations.push_back(m);

        db_ = std::make_unique<Database>(cfg_);
        db_->Open();
        db_->Initialize();

        db_ops_pool_ = std::make_unique<asio::thread_pool>(1u);

        backlog_ = std::make_unique<Backlog>(200);
        notifier_ = std::make_unique<LogNotifier>();

        ctx_ = std::make_unique<ServerContext>(ServerContext{
            cfg_,
            *db_,
            *backlog_,
            *notifier_,
            asio::make_strand(db_ops_pool_->get_executor()),
        });

        server_ = std::make_unique<Server>(*ctx_, 2u);

        server_thread_ = std::thread{[this]() { server_->Run(); }};

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        server_.reset();

        // Destroy context first — strand destructor posts cleanup to the
        // executor, which must still be alive.
        ctx_.reset();

        // Strand is dead; safe to tear down the pool.
        if (db_ops_pool_) {
            db_ops_pool_->stop();
            db_ops_pool_->join();
            db_ops_pool_.reset();
        }

        db_->Close();
        db_.reset();
        fs::remove_all(tmp_);
    }

    fs::path tmp_;
    Config cfg_;
    std::unique_ptr<Database> db_;
    std::unique_ptr<Backlog> backlog_;
    std::unique_ptr<LogNotifier> notifier_;
    std::unique_ptr<ServerContext> ctx_;
    std::unique_ptr<asio::thread_pool> db_ops_pool_;
    std::unique_ptr<Server> server_;
    std::thread server_thread_;
};

// ── Health endpoint ─────────────────────────────────────────────────────────

TEST_F(ServerTest, HealthReturnsOk) {
    auto res = http_req("127.0.0.1", 17788, http::verb::get, "/health");
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["status"], "ok");
}

// ── CORS preflight ──────────────────────────────────────────────────────────

TEST_F(ServerTest, OptionsReturnsNoContentWithCorsHeaders) {
    auto res = http_req("127.0.0.1", 17788, http::verb::options, "/logs");
    EXPECT_EQ(res.result(), http::status::no_content);
    EXPECT_EQ(res[http::field::access_control_allow_origin], "*");
    EXPECT_TRUE(res[http::field::access_control_allow_methods].contains("GET"));
}

// ── Insert ──────────────────────────────────────────────────────────────────

TEST_F(ServerTest, InsertSingleLog) {
    auto res = http_req("127.0.0.1", 17788, http::verb::post, "/logs",
                        R"({"timestamp":"2024-01-01T00:00:00Z","message":"hello","level":"INFO"})",
                        "application/json");
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["status"], "accepted");
}

TEST_F(ServerTest, InsertArrayOfLogs) {
    auto payload = R"([
        {"timestamp":"2024-01-01T00:00:00Z","message":"a","level":"INFO"},
        {"timestamp":"2024-01-01T00:00:01Z","message":"b","level":"ERROR"}
    ])";
    auto res = http_req("127.0.0.1", 17788, http::verb::post, "/logs", payload, "application/json");
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["status"], "accepted");
}

TEST_F(ServerTest, InsertInvalidJson) {
    auto res =
        http_req("127.0.0.1", 17788, http::verb::post, "/logs", "not json", "application/json");
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(ServerTest, InsertNonJsonObject) {
    auto res = http_req("127.0.0.1", 17788, http::verb::post, "/logs", "42", "application/json");
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

// ── Query ───────────────────────────────────────────────────────────────────

TEST_F(ServerTest, QueryEmptyDbReturnsEmpty) {
    auto res = http_req("127.0.0.1", 17788, http::verb::get, "/logs?fields=*&limit=10&offset=0");
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 0);
    EXPECT_TRUE(body["results"].empty());
}

TEST_F(ServerTest, QueryMissingFieldsReturns400) {
    auto res = http_req("127.0.0.1", 17788, http::verb::get, "/logs?limit=10&offset=0");
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(ServerTest, QueryWithDataReturnsResults) {
    // Insert via DB directly
    nlohmann::json log1{
        {"timestamp", "2024-01-01T00:00:00Z"}, {"message", "hello"}, {"level", "INFO"}};
    nlohmann::json log2{
        {"timestamp", "2024-01-01T00:00:01Z"}, {"message", "world"}, {"level", "ERROR"}};
    db_->Insert({log1, log2});

    auto res = http_req("127.0.0.1", 17788, http::verb::get, "/logs?fields=*&limit=10&offset=0");
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 2);
    EXPECT_EQ(body["results"].size(), 2u);
}

TEST_F(ServerTest, QueryWithFilter) {
    nlohmann::json log1{
        {"timestamp", "2024-01-01T00:00:00Z"}, {"message", "a"}, {"level", "DEBUG"}};
    nlohmann::json log2{
        {"timestamp", "2024-01-01T00:00:01Z"}, {"message", "b"}, {"level", "ERROR"}};
    db_->Insert({log1, log2});

    auto res = http_req("127.0.0.1", 17788, http::verb::get,
                        "/logs?fields=*&limit=10&offset=0&level==ERROR");
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 1);
    EXPECT_EQ(body["results"][0]["level"], "ERROR");
}

TEST_F(ServerTest, QueryPagination) {
    std::vector<nlohmann::json> logs;
    for (int i = 0; i < 5; ++i) {
        logs.push_back({
            {"timestamp", std::format("2024-01-01T00:00:{:02d}Z", i)},
            {"message", std::format("msg{}", i)},
            {"level", "INFO"},
        });
    }
    db_->Insert(logs);

    auto res = http_req("127.0.0.1", 17788, http::verb::get, "/logs?fields=*&limit=2&offset=0");
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 5);
    EXPECT_EQ(body["results"].size(), 2u);
}

TEST_F(ServerTest, QueryWithSpecificFields) {
    nlohmann::json log1{
        {"timestamp", "2024-01-01T00:00:00Z"}, {"message", "hello"}, {"level", "INFO"}};
    db_->Insert({log1});

    auto res = http_req("127.0.0.1", 17788, http::verb::get,
                        "/logs?fields=message,level&limit=10&offset=0");
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    ASSERT_EQ(body["results"].size(), 1u);
    EXPECT_TRUE(body["results"][0].contains("message"));
    EXPECT_TRUE(body["results"][0].contains("level"));
}

TEST_F(ServerTest, QueryNonNumericLimit) {
    auto res = http_req("127.0.0.1", 17788, http::verb::get, "/logs?fields=*&limit=abc&offset=0");
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(ServerTest, QueryNonNumericOffset) {
    auto res = http_req("127.0.0.1", 17788, http::verb::get, "/logs?fields=*&limit=10&offset=xxx");
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

// ── 404 ─────────────────────────────────────────────────────────────────────

TEST_F(ServerTest, UnknownRouteReturns404) {
    auto res = http_req("127.0.0.1", 17788, http::verb::get, "/nonexistent");
    EXPECT_EQ(static_cast<int>(res.result()), 404);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["error"], "not found");
}

TEST_F(ServerTest, PostToHealthReturns404) {
    auto res = http_req("127.0.0.1", 17788, http::verb::post, "/health");
    EXPECT_EQ(static_cast<int>(res.result()), 404);
}

// ── Multiple connections ────────────────────────────────────────────────────

TEST_F(ServerTest, MultipleRequestsSequentially) {
    // Health
    auto res1 = http_req("127.0.0.1", 17788, http::verb::get, "/health");
    EXPECT_EQ(res1.result(), http::status::ok);

    // Insert
    auto res2 = http_req("127.0.0.1", 17788, http::verb::post, "/logs",
                         R"({"timestamp":"2024-01-01T00:00:00Z","message":"multi","level":"INFO"})",
                         "application/json");
    EXPECT_EQ(res2.result(), http::status::ok);

    // Query (data was inserted via backlog, need to flush)
    // Insert directly for the test
    db_->Insert(
        {{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "direct"}, {"level", "INFO"}}});

    auto res3 = http_req("127.0.0.1", 17788, http::verb::get, "/logs?fields=*&limit=10&offset=0");
    EXPECT_EQ(res3.result(), http::status::ok);

    auto body = nlohmann::json::parse(res3.body());
    EXPECT_GE(body["total"], 1);
}

// ── SSE headers ────────────────────────────────────────────────────────────

TEST_F(ServerTest, SSEReturnsChunkedResponse) {
    asio::io_context ioc;
    tcp::socket socket{ioc};
    tcp::resolver resolver{ioc};
    auto endpoints = resolver.resolve("127.0.0.1", "17788");
    asio::connect(socket, endpoints);

    http::request<http::string_body> req{http::verb::get, "/logs/sse?fields=*", 11};
    req.set(http::field::host, "127.0.0.1");
    http::write(socket, req);

    // Read just the response header
    beast::flat_buffer buf;
    http::response_parser<http::empty_body> parser;
    http::read_header(socket, buf, parser);

    auto res = parser.get();
    EXPECT_EQ(res.result(), http::status::ok);
    EXPECT_EQ(res[http::field::content_type], "text/event-stream");
    EXPECT_EQ(res[http::field::cache_control], "no-cache");
    EXPECT_EQ(res.chunked(), true);

    beast::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
}

TEST_F(ServerTest, SSEWithFieldsParam) {
    asio::io_context ioc;
    tcp::socket socket{ioc};
    tcp::resolver resolver{ioc};
    auto endpoints = resolver.resolve("127.0.0.1", "17788");
    asio::connect(socket, endpoints);

    http::request<http::string_body> req{http::verb::get, "/logs/sse?fields=message,level", 11};
    req.set(http::field::host, "127.0.0.1");
    http::write(socket, req);

    beast::flat_buffer buf;
    http::response_parser<http::empty_body> parser;
    http::read_header(socket, buf, parser);

    auto res = parser.get();
    EXPECT_EQ(res.result(), http::status::ok);

    beast::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
}

// ── Handle connection error ─────────────────────────────────────────────────

TEST_F(ServerTest, ImmediateDisconnectIsHandled) {
    asio::io_context ioc;
    tcp::socket socket{ioc};
    tcp::resolver resolver{ioc};
    auto endpoints = resolver.resolve("127.0.0.1", "17788");
    asio::connect(socket, endpoints);

    // Disconnect immediately without sending anything
    socket.close();
    // No crash expected — server handles this gracefully
    SUCCEED();
}
