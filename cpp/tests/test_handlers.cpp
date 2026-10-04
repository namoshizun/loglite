#include <gtest/gtest.h>

#include "handlers/common.hpp"
#include "handlers/health.hpp"
#include "handlers/version_route.hpp"
#include "handlers/settings.hpp"
#include "handlers/schema.hpp"
#include "version.hpp"
#include "handlers/insert.hpp"
#include "handlers/query.hpp"
#include "test_support.hpp"
#include "handlers/stats.hpp"
#include "writer_database.hpp"
#include "context.hpp"
#include "backlog.hpp"
#include "metrics.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <boost/asio.hpp>
#include <future>

namespace asio = boost::asio;
namespace http = boost::beast::http;
using namespace loglite;

template <typename T>
static T SyncAwait(asio::awaitable<T> coro) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, std::move(coro), asio::use_future);
    ioc.run();
    return fut.get();
}

class HandlersTest : public ::testing::Test {
   protected:
    void SetUp() override {
        metrics::MetricsRegistry::Instance().Reset();

        db_ = std::make_unique<WriterDatabase>(cfg_);
        db_->Open();
        db_->Initialize();

        backlog_ = std::make_unique<Backlog>(200);
        notifier_ = std::make_unique<LogNotifier>();

        db_ops_pool_ = std::make_unique<asio::thread_pool>(1u);
        reader_pool_ = std::make_unique<asio::thread_pool>(1u);
        db_read_ = std::make_unique<ReadDatabasePool>(cfg_, db_->catalog(), 1u);

        ctx_ = std::make_unique<ServerContext>(cfg_, *db_, *db_read_, *backlog_, *notifier_,
                                               asio::make_strand(db_ops_pool_->get_executor()),
                                               reader_pool_->get_executor());
    }

    void TearDown() override {
        ctx_.reset();
        db_read_->Close();
        db_read_.reset();
        reader_pool_->join();
        reader_pool_.reset();
        db_ops_pool_->join();
        db_ops_pool_.reset();
        db_->Close();
        db_.reset();
        metrics::MetricsRegistry::Instance().Reset();
    }

    http::request<http::string_body> MakeRequest(http::verb method, std::string target,
                                                 std::string body = "") {
        http::request<http::string_body> req{method, target, 11};
        req.set(http::field::host, "127.0.0.1");
        if (!body.empty()) {
            req.body() = std::move(body);
            req.prepare_payload();
        }
        return req;
    }

    test::TempDirectory directory_;
    Config cfg_{test::MakeConfig(directory_.path())};
    std::unique_ptr<WriterDatabase> db_;
    std::unique_ptr<ReadDatabasePool> db_read_;
    std::unique_ptr<Backlog> backlog_;
    std::unique_ptr<LogNotifier> notifier_;
    std::unique_ptr<asio::thread_pool> db_ops_pool_;
    std::unique_ptr<asio::thread_pool> reader_pool_;
    std::unique_ptr<ServerContext> ctx_;
};

// ── Health handler ──────────────────────────────────────────────────────────

TEST_F(HandlersTest, HealthReturnsStatusAndCorsHeaders) {
    auto req = MakeRequest(http::verb::get, "/health");
    auto res = SyncAwait(handlers::HandleHealth(req, *ctx_));
    EXPECT_EQ(res.result(), http::status::ok);
    EXPECT_EQ(nlohmann::json::parse(res.body())["status"], "ok");
    EXPECT_EQ(res[http::field::access_control_allow_origin], cfg_.allow_origin);
}

TEST_F(HandlersTest, SettingsReturnsConfiguredValues) {
    cfg_.log_table_name = "MyLogs";
    cfg_.db_pool_size = "8";
    cfg_.compression.enabled = true;
    cfg_.harvesters.push_back(Config::HarvesterDef{
        .type = "loglite.harvesters.FileHarvester",
        .name = "files",
        .config = {},
    });

    auto req = MakeRequest(http::verb::get, "/settings");
    auto res = SyncAwait(handlers::HandleSettings(req, *ctx_));
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

    auto pool = find_key("db_pool_size");
    ASSERT_FALSE(pool.is_null());
    EXPECT_EQ(pool["value"], "8");
    EXPECT_TRUE(pool["description"].is_string());

    auto compression = find_key("compression_enabled");
    ASSERT_FALSE(compression.is_null());
    EXPECT_EQ(compression["value"], true);

    auto types = find_key("harvester_types");
    ASSERT_FALSE(types.is_null());
    ASSERT_EQ(types["value"].size(), 1u);
    EXPECT_EQ(types["value"][0], "loglite.harvesters.FileHarvester");
}

TEST_F(HandlersTest, SchemaReturnsLogTableColumns) {
    auto req = MakeRequest(http::verb::get, "/schema");
    auto res = SyncAwait(handlers::HandleSchema(req, *ctx_));
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
    auto req = MakeRequest(http::verb::get, "/version");
    auto res = SyncAwait(handlers::HandleVersion(req, *ctx_));
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["version"], kVersion);
}

// ── Insert handler ──────────────────────────────────────────────────────────

TEST_F(HandlersTest, IngestionPreservesObjectAndArrayPayloadsAndRecordsRequestSize) {
    const std::vector<nlohmann::json> logs{
        {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "hello"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "world"}, {"level", "ERROR"}},
    };
    for (bool array : {false, true}) {
        SCOPED_TRACE(array ? "array" : "object");
        const auto payload = array ? nlohmann::json(logs) : logs[0];
        auto req = MakeRequest(http::verb::post, "/logs", payload.dump());
        auto res = SyncAwait(handlers::HandleInsert(req, *ctx_));
        EXPECT_EQ(res.result(), http::status::ok);
        EXPECT_EQ(nlohmann::json::parse(res.body())["status"], "accepted");
        EXPECT_EQ(backlog_->Flush(), array ? logs : std::vector<nlohmann::json>{logs[0]});
        const auto samples = metrics::MetricsRegistry::Instance().Flush();
        ASSERT_EQ(samples.size(), 1u);
        EXPECT_EQ(samples[0].name, metrics::kIngestRequest);
        EXPECT_DOUBLE_EQ(samples[0].value, req.body().size());
    }
}

TEST_F(HandlersTest, InvalidIngestionDoesNotEnqueueData) {
    for (const auto* payload : {"not json", "42", "null", "true", "\"string\""}) {
        SCOPED_TRACE(payload);
        auto req = MakeRequest(http::verb::post, "/logs", payload);
        auto res = SyncAwait(handlers::HandleInsert(req, *ctx_));
        EXPECT_EQ(res.result(), http::status::bad_request);
        const auto error = nlohmann::json::parse(res.body())["error"].get<std::string>();
        if (std::string_view{payload} == "not json")
            EXPECT_TRUE(error.starts_with("Invalid JSON"));
        else
            EXPECT_EQ(error, "Body must be a JSON object or array");
        EXPECT_EQ(backlog_->Size(), 0u);
    }
}

// ── Query handler ───────────────────────────────────────────────────────────

TEST_F(HandlersTest, QueryValidationRejectsInvalidRequestsAndRecordsMetrics) {
    struct Case {
        std::string target, error;
    };
    const Case cases[]{
        {"/logs?limit=10&offset=0", "Required parameter 'fields' is missing"},
        {"/logs?fields=*&offset=0", "Required parameter 'limit' is missing"},
        {"/logs?fields=*&limit=10", "Required parameter 'offset' is missing"},
        {"/logs?fields=*&limit=abc&offset=0", "Parameters 'limit' and 'offset' must be integers"},
        {"/logs?fields=*&limit=0&offset=0", "'limit' must be a positive integer"},
        {"/logs?fields=*&limit=-1&offset=0", "'limit' must be a positive integer"},
        {"/logs?fields=*&limit=9999999999999999999&offset=0",
         "Parameters 'limit' and 'offset' must be integers"},
        {fmt::format("/logs?fields=*&limit={}&offset=0", kMaxQueryLimit + 1),
         fmt::format("'limit' must not exceed {}", kMaxQueryLimit)},
        {"/logs?fields=*&limit=2147483647&offset=0",
         fmt::format("'limit' must not exceed {}", kMaxQueryLimit)},
        {"/logs?fields=*&limit=10&offset=abc", "Parameters 'limit' and 'offset' must be integers"},
        {"/logs?fields=*&limit=10&offset=-1", "'offset' must be a non-negative integer"},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.target);
        auto req = MakeRequest(http::verb::get, c.target);
        auto res = SyncAwait(handlers::HandleQuery(req, *ctx_));
        EXPECT_EQ(res.result(), http::status::bad_request);
        EXPECT_EQ(nlohmann::json::parse(res.body())["error"], c.error);
        const auto samples = metrics::MetricsRegistry::Instance().Flush();
        ASSERT_EQ(samples.size(), 1u);
        EXPECT_EQ(samples[0].name, metrics::kQueryRequest);
        EXPECT_GE(samples[0].value, 0.0);
    }
}

TEST_F(HandlersTest, EmptyQueriesAcceptBothLimitBoundaries) {
    for (int limit : {1, kMaxQueryLimit}) {
        SCOPED_TRACE(limit);
        auto req =
            MakeRequest(http::verb::get, fmt::format("/logs?fields=*&limit={}&offset=0", limit));
        auto res = SyncAwait(handlers::HandleQuery(req, *ctx_));
        EXPECT_EQ(res.result(), http::status::ok);
        const auto body = nlohmann::json::parse(res.body());
        EXPECT_EQ(body["total"], 0);
        EXPECT_TRUE(body["results"].empty());
    }
}

TEST_F(HandlersTest, QueriesCombineFilteringProjectionAndPagination) {
    ASSERT_EQ(db_->Insert({
                  {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "first"}, {"level", "ERROR"}},
                  {{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "second"}, {"level", "INFO"}},
                  {{"timestamp", "2024-01-01T00:00:02Z"}, {"message", "third"}, {"level", "ERROR"}},
              }),
              3);
    struct Case {
        const char* target;
        int total;
        nlohmann::json results;
    };
    const Case cases[]{
        {"/logs?fields=message&limit=2&offset=0",
         3,
         {{{"message", "third"}}, {{"message", "second"}}}},
        {"/logs?fields=message&limit=2&offset=2", 3, {{{"message", "first"}}}},
        {"/logs?fields=message&limit=2&offset=3", 3, nlohmann::json::array()},
        {"/logs?fields=message,level&limit=1&offset=1&level==ERROR",
         2,
         {{{"message", "first"}, {"level", "ERROR"}}}},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.target);
        auto req = MakeRequest(http::verb::get, c.target);
        auto res = SyncAwait(handlers::HandleQuery(req, *ctx_));
        EXPECT_EQ(res.result(), http::status::ok);
        const auto body = nlohmann::json::parse(res.body());
        EXPECT_EQ(body["total"], c.total);
        EXPECT_EQ(body["results"], c.results);
    }
}

TEST_F(HandlersTest, QueryInvalidFilterExpression) {
    auto req = MakeRequest(http::verb::get, "/logs?fields=*&limit=10&offset=0&bad_field=novalue");
    auto res = SyncAwait(handlers::HandleQuery(req, *ctx_));
    EXPECT_EQ(static_cast<int>(res.result()), 400);
}

TEST_F(HandlersTest, QueryWithUnknownFieldInFilter) {
    nlohmann::json log1{
        {"timestamp", "2024-01-01T00:00:00Z"}, {"message", "hello"}, {"level", "INFO"}};
    db_->Insert({log1});

    auto req = MakeRequest(http::verb::get, "/logs?fields=*&limit=10&offset=0&nonexistent==val");
    auto res = SyncAwait(handlers::HandleQuery(req, *ctx_));
    EXPECT_EQ(static_cast<int>(res.result()), 500);
}

// ── Stats handler ─────────────────────────────────────────────────────────────

TEST_F(HandlersTest, StatsRejectsMissingParametersAndInvalidTimeWindows) {
    for (const auto* target :
         {"/stats", "/stats?since=2024-01-01T00:00:00Z",
          "/stats?since=2024-01-01T00:00:00Z&until=2024-01-01T01:00:00Z",
          "/stats?since=2024-01-01T00:00:00Z&until=2024-01-01T01:00:00Z&activity_stats_fields=*"}) {
        SCOPED_TRACE(target);
        auto req = MakeRequest(http::verb::get, target);
        EXPECT_EQ(SyncAwait(handlers::HandleStats(req, *ctx_)).result(), http::status::bad_request);
    }
    struct Case {
        const char* since;
        const char* until;
        const char* ordering;
        const char* error;
    };
    const Case cases[]{
        {"2024-01-01T00:00:00Z", "2024-01-03T00:00:00Z", "desc",
         "Time window must not exceed 1 day"},
        {"2024-01-02T00:00:00Z", "2024-01-01T00:00:00Z", "desc", "'until' must be after 'since'"},
        {"2024-01-01T00:00:00Z", "2024-01-01T00:00:00Z", "desc", "'until' must be after 'since'"},
        {"notatime", "2024-01-01T01:00:00Z", "desc",
         "'since' and 'until' must be ISO-8601 timestamps"},
        {"2024-01-01T00:00:00Z", "notatime", "desc",
         "'since' and 'until' must be ISO-8601 timestamps"},
        {"2024-01-01T00:00:00Z", "2024-01-01T01:00:00Z", "sideways",
         "Parameter 'ordering' must be 'asc' or 'desc'"},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.error);
        auto req = MakeRequest(http::verb::get,
                               fmt::format("/stats?since={}&until={}&ordering={}&activity_stats_"
                                           "fields=*&database_stats_fields=*",
                                           c.since, c.until, c.ordering));
        auto res = SyncAwait(handlers::HandleStats(req, *ctx_));
        EXPECT_EQ(res.result(), http::status::bad_request);
        EXPECT_EQ(nlohmann::json::parse(res.body())["error"], c.error);
    }
}

TEST_F(HandlersTest, StatsAcceptsFractionalSecondsOffsetsAndMaximumWindow) {
    struct Case {
        const char* since;
        const char* until;
    };
    const Case cases[]{
        {"2024-01-01T00:00:00.000Z", "2024-01-01T01:00:00.999Z"},
        // '+' must be %2B in query values — url_decode maps '+' to space.
        {"2024-06-15T08:30:00%2B08:00", "2024-06-15T09:30:00%2B08:00"},
        {"2024-06-15T08:30:00%2B0830", "2024-06-15T09:30:00%2B0830"},
        {"2024-06-14T19:30:00.500-05:00", "2024-06-14T20:30:00.250-05:00"},
        {"2024-01-01T00:00:00Z", "2024-01-02T00:00:00Z"},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.since);
        auto req = MakeRequest(
            http::verb::get,
            fmt::format("/stats?since={}&until={}&activity_stats_fields=*&database_stats_fields=*",
                        c.since, c.until));
        auto res = SyncAwait(handlers::HandleStats(req, *ctx_));
        EXPECT_EQ(res.result(), http::status::ok);
        const auto body = nlohmann::json::parse(res.body());
        EXPECT_TRUE(body["activities"]["data"].empty());
        EXPECT_FALSE(body["activities"]["fields"].empty());
        EXPECT_TRUE(body["database"]["data"].empty());
        EXPECT_FALSE(body["database"]["fields"].empty());
        EXPECT_GE(body["uptime"].get<int64_t>(), 0);
    }
}

TEST_F(HandlersTest, StatsTrimsFieldListsAndReturnsPersistedValues) {
    ASSERT_TRUE(db_->InsertActivityStats({.since = "2024-01-01T00:00:00Z",
                                          .until = "2024-01-01T00:01:00Z",
                                          .query_count = 10,
                                          .query_avg = 5}));
    ASSERT_TRUE(db_->InsertDatabaseStats({"2024-01-01T00:01:00Z", 100, 4096}));
    // Spaces after commas (and outer padding) via %20 — raw spaces in target break HTTP parsing.
    auto req = MakeRequest(http::verb::get,
                           "/stats?since=2024-01-01T00:00:00Z&until=2024-01-01T01:00:00Z"
                           "&activity_stats_fields=query_count%2C%20query_avg"
                           "&database_stats_fields=%20rows_count%20%2C%20db_size%20&ordering=asc");
    auto res = SyncAwait(handlers::HandleStats(req, *ctx_));
    EXPECT_EQ(res.result(), http::status::ok);
    const auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["activities"]["fields"], (std::vector<std::string>{"query_count", "query_avg"}));
    EXPECT_EQ(body["activities"]["data"], nlohmann::json::array({{10, 5}}));
    EXPECT_EQ(body["database"]["fields"], (std::vector<std::string>{"rows_count", "db_size"}));
    EXPECT_EQ(body["database"]["data"], nlohmann::json::array({{100, 4096}}));
}
