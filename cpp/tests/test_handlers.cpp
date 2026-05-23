#include <gtest/gtest.h>

#include "handlers/common.hpp"
#include "handlers/health.hpp"
#include "handlers/version_route.hpp"
#include "handlers/settings.hpp"
#include "handlers/schema.hpp"
#include "version.hpp"
#include "handlers/insert.hpp"
#include "handlers/query.hpp"
#include "config.hpp"
#include "writer_database.hpp"
#include "globals.hpp"
#include "backlog.hpp"
#include "metrics.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <boost/asio.hpp>
#include <filesystem>

namespace fs = std::filesystem;
namespace asio = boost::asio;
namespace http = boost::beast::http;
using namespace loglite;

class HandlersTest : public ::testing::Test {
   protected:
    void SetUp() override {
        metrics::MetricsRegistry::Instance().Reset();

        tmp_ = fs::temp_directory_path() / "loglite_handlers_test";
        fs::remove_all(tmp_);
        fs::create_directories(tmp_);

        cfg_.sqlite_dir = tmp_;
        cfg_.db_path = tmp_ / "logs.db";
        cfg_.log_table_name = "TestLog";
        cfg_.log_timestamp_field = "timestamp";
        cfg_.auto_rollout = true;
        cfg_.compression = {false, {}};
        cfg_.host = "127.0.0.1";
        cfg_.port = 7788;
        cfg_.allow_origin = "*";

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

        db_ = std::make_unique<WriterDatabase>(cfg_);
        db_->Open();
        db_->Initialize();

        backlog_ = std::make_unique<Backlog>(200);
        notifier_ = std::make_unique<LogNotifier>();

        db_ops_pool_ = std::make_unique<asio::thread_pool>(1u);
        db_read_ = std::make_unique<ReadDatabasePool>(cfg_, db_->catalog(), 1u);

        ctx_ = std::make_unique<ServerContext>(ServerContext{
            cfg_,
            *db_,
            *db_read_,
            *backlog_,
            *notifier_,
            asio::make_strand(db_ops_pool_->get_executor()),
        });
    }

    void TearDown() override {
        ctx_.reset();
        db_read_->Close();
        db_read_.reset();
        db_ops_pool_->join();
        db_ops_pool_.reset();
        db_->Close();
        db_.reset();
        fs::remove_all(tmp_);
    }

    http::request<http::string_body> make_req(http::verb method, std::string target,
                                              std::string body = "") {
        http::request<http::string_body> req{method, target, 11};
        req.set(http::field::host, "127.0.0.1");
        if (!body.empty()) {
            req.body() = std::move(body);
            req.prepare_payload();
        }
        return req;
    }

    fs::path tmp_;
    Config cfg_;
    std::unique_ptr<WriterDatabase> db_;
    std::unique_ptr<ReadDatabasePool> db_read_;
    std::unique_ptr<Backlog> backlog_;
    std::unique_ptr<LogNotifier> notifier_;
    std::unique_ptr<asio::thread_pool> db_ops_pool_;
    std::unique_ptr<ServerContext> ctx_;
};

// ── Health handler ──────────────────────────────────────────────────────────

TEST_F(HandlersTest, HealthReturnsOk) {
    auto req = make_req(http::verb::get, "/health");
    auto res = handlers::HandleHealth(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["status"], "ok");
}

TEST_F(HandlersTest, HealthContainsCorsHeaders) {
    auto req = make_req(http::verb::get, "/health");
    auto res = handlers::HandleHealth(req, *ctx_);
    EXPECT_EQ(res[http::field::access_control_allow_origin], "*");
}

TEST_F(HandlersTest, SettingsReturnsConfiguredValues) {
    cfg_.log_table_name = "MyLogs";
    cfg_.compression.enabled = true;
    cfg_.harvesters.push_back(Config::HarvesterDef{
        .type = "loglite.harvesters.FileHarvester",
        .name = "files",
        .config = {},
    });

    auto req = make_req(http::verb::get, "/settings");
    auto res = handlers::HandleSettings(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    ASSERT_TRUE(body.contains("settings"));
    auto settings = body["settings"];
    ASSERT_TRUE(settings.is_array());

    auto find_key = [&](const char* key) -> nlohmann::json {
        for (const auto& item : settings) {
            if (item["key"] == key) return item;
        }
        return nlohmann::json();
    };

    auto table = find_key("log_table_name");
    ASSERT_FALSE(table.is_null());
    EXPECT_EQ(table["value"], "MyLogs");
    EXPECT_TRUE(table["description"].is_string());

    auto compression = find_key("compression_enabled");
    ASSERT_FALSE(compression.is_null());
    EXPECT_EQ(compression["value"], true);

    auto types = find_key("harvester_types");
    ASSERT_FALSE(types.is_null());
    ASSERT_EQ(types["value"].size(), 1u);
    EXPECT_EQ(types["value"][0], "loglite.harvesters.FileHarvester");
}

TEST_F(HandlersTest, SchemaReturnsLogTableColumns) {
    auto req = make_req(http::verb::get, "/schema");
    auto res = handlers::HandleSchema(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["table"], "TestLog");
    ASSERT_TRUE(body["columns"].is_array());
    ASSERT_GE(body["columns"].size(), 4u);

    auto find_col = [&](const char* name) -> nlohmann::json {
        for (const auto& col : body["columns"]) {
            if (col["name"] == name) return col;
        }
        return nlohmann::json();
    };

    auto id = find_col("id");
    ASSERT_FALSE(id.is_null());
    EXPECT_EQ(id["kind"], "integer");
    EXPECT_TRUE(id["primary_key"].get<bool>());

    auto ts = find_col("timestamp");
    ASSERT_FALSE(ts.is_null());
    EXPECT_EQ(ts["kind"], "text");
    EXPECT_EQ(ts["sqlite_type"], "TEXT");

    auto level = find_col("level");
    ASSERT_FALSE(level.is_null());
    EXPECT_EQ(level["kind"], "text");
    EXPECT_FALSE(level["compressed"].get<bool>());
}

TEST_F(HandlersTest, VersionReturnsProjectVersion) {
    auto req = make_req(http::verb::get, "/version");
    auto res = handlers::HandleVersion(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["version"], kVersion);
}

// ── Insert handler ──────────────────────────────────────────────────────────

TEST_F(HandlersTest, InsertSingleObject) {
    auto req = make_req(http::verb::post, "/logs",
                        R"({"timestamp":"2024-01-01T00:00:00Z","message":"hello","level":"INFO"})");
    auto res = handlers::HandleInsert(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["status"], "accepted");
    EXPECT_EQ(backlog_->Size(), 1u);
}

TEST_F(HandlersTest, InsertRecordsPayloadSizeMetric) {
    std::string body = R"({"timestamp":"2024-01-01T00:00:00Z","message":"hello","level":"INFO"})";
    auto req = make_req(http::verb::post, "/logs", body);
    auto res = handlers::HandleInsert(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto samples = metrics::MetricsRegistry::Instance().Flush();
    ASSERT_EQ(samples.size(), 1u);
    EXPECT_EQ(samples[0].name, metrics::kIngestRequest);
    EXPECT_DOUBLE_EQ(samples[0].value, static_cast<double>(body.size()));
}

TEST_F(HandlersTest, InsertArray) {
    auto req = make_req(
        http::verb::post, "/logs",
        R"([{"timestamp":"2024-01-01T00:00:00Z","message":"a","level":"INFO"},{"timestamp":"2024-01-01T00:00:01Z","message":"b","level":"ERROR"}])");
    auto res = handlers::HandleInsert(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["status"], "accepted");
    EXPECT_EQ(backlog_->Size(), 2u);
}

TEST_F(HandlersTest, InsertInvalidJson) {
    auto req = make_req(http::verb::post, "/logs", "not json");
    auto res = handlers::HandleInsert(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_TRUE(body["error"].get<std::string>().find("Invalid JSON") != std::string::npos);
}

TEST_F(HandlersTest, InsertWrongType) {
    auto req = make_req(http::verb::post, "/logs", "42");
    auto res = handlers::HandleInsert(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["error"], "Body must be a JSON object or array");
}

// ── Query handler ───────────────────────────────────────────────────────────

TEST_F(HandlersTest, QueryMissingFieldsParam) {
    auto req = make_req(http::verb::get, "/logs?limit=10&offset=0");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(HandlersTest, QueryRecordsRequestMetricOnValidationFailure) {
    auto req = make_req(http::verb::get, "/logs?limit=10&offset=0");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);

    auto samples = metrics::MetricsRegistry::Instance().Flush();
    ASSERT_EQ(samples.size(), 1u);
    EXPECT_EQ(samples[0].name, metrics::kQueryRequest);
    EXPECT_GE(samples[0].value, 0.0);
}

TEST_F(HandlersTest, QueryMissingLimitParam) {
    auto req = make_req(http::verb::get, "/logs?fields=*&offset=0");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(HandlersTest, QueryMissingOffsetParam) {
    auto req = make_req(http::verb::get, "/logs?fields=*&limit=10");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(HandlersTest, QueryWithEmptyDbReturnsEmpty) {
    auto req = make_req(http::verb::get, "/logs?fields=*&limit=10&offset=0");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 0);
    EXPECT_TRUE(body["results"].empty());
}

TEST_F(HandlersTest, QueryReturnsInsertedLogs) {
    // Insert logs into DB directly via backlog flush
    nlohmann::json log1{
        {"timestamp", "2024-01-01T00:00:00Z"}, {"message", "hello"}, {"level", "INFO"}};
    nlohmann::json log2{
        {"timestamp", "2024-01-01T00:00:01Z"}, {"message", "world"}, {"level", "ERROR"}};
    db_->Insert({log1, log2});

    auto req = make_req(http::verb::get, "/logs?fields=*&limit=10&offset=0");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 2);
    EXPECT_EQ(body["results"].size(), 2u);
}

TEST_F(HandlersTest, QueryWithFilter) {
    nlohmann::json log1{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "a"}, {"level", "INFO"}};
    nlohmann::json log2{
        {"timestamp", "2024-01-01T00:00:01Z"}, {"message", "b"}, {"level", "ERROR"}};
    db_->Insert({log1, log2});

    auto req = make_req(http::verb::get, "/logs?fields=*&limit=10&offset=0&level==ERROR");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 1);
    EXPECT_EQ(body["results"][0]["level"], "ERROR");
}

TEST_F(HandlersTest, QueryNonNumericLimit) {
    auto req = make_req(http::verb::get, "/logs?fields=*&limit=abc&offset=0");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(HandlersTest, QueryNonNumericOffset) {
    auto req = make_req(http::verb::get, "/logs?fields=*&limit=10&offset=abc");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(HandlersTest, QueryInvalidFilterExpression) {
    auto req = make_req(http::verb::get, "/logs?fields=*&limit=10&offset=0&bad_field=novalue");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(HandlersTest, QuerySpecificFields) {
    nlohmann::json log1{
        {"timestamp", "2024-01-01T00:00:00Z"}, {"message", "hello"}, {"level", "INFO"}};
    db_->Insert({log1});

    auto req = make_req(http::verb::get, "/logs?fields=message,level&limit=10&offset=0");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 1);
    ASSERT_EQ(body["results"].size(), 1u);
    EXPECT_TRUE(body["results"][0].contains("message"));
    EXPECT_TRUE(body["results"][0].contains("level"));
    EXPECT_FALSE(body["results"][0].contains("timestamp"));
}

TEST_F(HandlersTest, QueryWithUnknownFieldInFilter) {
    nlohmann::json log1{
        {"timestamp", "2024-01-01T00:00:00Z"}, {"message", "hello"}, {"level", "INFO"}};
    db_->Insert({log1});

    auto req = make_req(http::verb::get, "/logs?fields=*&limit=10&offset=0&nonexistent==val");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(static_cast<int>(res.result()), 500);
}

TEST_F(HandlersTest, QueryPagination) {
    std::vector<nlohmann::json> logs;
    for (int i = 0; i < 5; ++i) {
        logs.push_back({
            {"timestamp", fmt::format("2024-01-01T00:00:{:02d}Z", i)},
            {"message", fmt::format("msg{}", i)},
            {"level", "INFO"},
        });
    }
    db_->Insert(logs);

    auto req = make_req(http::verb::get, "/logs?fields=*&limit=2&offset=0");
    auto res = handlers::HandleQuery(req, *ctx_);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 5);
    EXPECT_EQ(body["results"].size(), 2u);
}

// ── Response helpers ────────────────────────────────────────────────────────

TEST(ResponseHelperTest, MakeFailResp) {
    http::request<http::string_body> req{http::verb::get, "/test", 11};
    auto res = handlers::MakeFailResp(404, "not found", req);
    EXPECT_EQ(static_cast<int>(res.result()), 404);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["error"], "not found");
}

TEST(ResponseHelperTest, MakeOKResp) {
    http::request<http::string_body> req{http::verb::get, "/test", 11};
    auto res = handlers::MakeOKResp({{"key", "value"}}, req);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["key"], "value");
}

TEST(ResponseHelperTest, MakeNotAvailableResp) {
    http::request<http::string_body> req{http::verb::get, "/test", 11};
    auto res = handlers::MakeNotAvailableResp({{"msg", "busy"}}, req);
    EXPECT_EQ(res.result(), http::status::service_unavailable);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["msg"], "busy");
}
