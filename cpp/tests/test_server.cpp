#include <gtest/gtest.h>

#include "test_support.hpp"
#include "log_store.hpp"
#include "context.hpp"
#include "metrics.hpp"
#include "log_reader.hpp"
#include "server.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <fmt/format.h>
#include <future>
#include <latch>
#include <thread>

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

static std::vector<http::response<http::string_body>> http_req_keep_alive(
    const std::string& host, uint16_t port, http::verb method, std::string_view target,
    std::string_view body, std::string_view content_type, unsigned int repeat) {
    asio::io_context ioc;
    tcp::socket socket{ioc};
    tcp::resolver resolver{ioc};
    auto endpoints = resolver.resolve(host, std::to_string(port));
    asio::connect(socket, endpoints);

    beast::tcp_stream stream{std::move(socket)};
    beast::flat_buffer buf;
    std::vector<http::response<http::string_body>> responses;
    responses.reserve(repeat);

    for (unsigned i = 0; i < repeat; ++i) {
        http::request<http::string_body> req{method, std::string(target), 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "loglite-test");
        req.keep_alive(i + 1 < repeat);
        req.body() = std::string(body);
        req.set(http::field::content_type, content_type);
        req.prepare_payload();

        http::write(stream, req);

        http::response<http::string_body> res;
        http::read(stream, buf, res);
        responses.push_back(std::move(res));
    }

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return responses;
}

// ── Fixture ──────────────────────────────────────────────────────────────────

// Reads SSE framing without depending on individual TCP read boundaries.
class SSEClient {
   public:
    SSEClient(const Config& cfg, std::string_view target = "/logs/sse") : socket_(io_) {
        socket_.connect({asio::ip::make_address(cfg.host), cfg.port});
        http::request<http::empty_body> request{http::verb::get, std::string(target), 11};
        request.set(http::field::host, cfg.host);
        request.set("Last-Event-ID", "0");  // reconnections still start at registration
        http::write(socket_, request);
        beast::flat_buffer buffer;
        http::response_parser<http::empty_body> parser;
        http::read_header(socket_, buffer, parser);
        EXPECT_EQ(parser.get().result(), http::status::ok);
        EXPECT_EQ(parser.get()[http::field::content_type], "text/event-stream");
        EXPECT_EQ(parser.get()[http::field::cache_control], "no-cache");
        EXPECT_EQ(parser.get()[http::field::access_control_allow_origin], cfg.allow_origin);
        EXPECT_EQ(parser.get()["X-Accel-Buffering"], "no");
        EXPECT_TRUE(parser.get().chunked());
        pending_ = beast::buffers_to_string(buffer.data());
        socket_.non_blocking(true);
    }

    bool AwaitEvents(size_t count, std::chrono::milliseconds timeout = std::chrono::seconds{3}) {
        return test::WaitUntil(
            [&] {
                ReadAvailable();
                return events.size() >= count;
            },
            timeout);
    }

    bool AwaitHeartbeat() {
        return test::WaitUntil(
            [&] {
                ReadAvailable();
                return pending_.find(":\r\n\r\n") != std::string::npos;
            },
            std::chrono::seconds{17});
    }

    void Disconnect() {
        socket_.set_option(asio::socket_base::linger{true, 0});
        socket_.close();
    }

    std::vector<nlohmann::json> events;

   private:
    void ReadAvailable() {
        char bytes[4096];
        boost::system::error_code error;
        const auto count = socket_.read_some(asio::buffer(bytes), error);
        if (error && error != asio::error::would_block && error != asio::error::try_again)
            throw boost::system::system_error(error);
        pending_.append(bytes, count);
        for (;;) {
            const auto start = pending_.find("data: ");
            if (start == std::string::npos) break;
            const auto end = pending_.find("\r\n\r\n", start);
            if (end == std::string::npos) break;
            events.push_back(nlohmann::json::parse(pending_.substr(start + 6, end - start - 6)));
            pending_.erase(0, end + 4);
        }
    }

    asio::io_context io_;
    tcp::socket socket_;
    std::string pending_;
};

class ServerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        metrics::MetricsRegistry::Instance().Reset();

        cfg_.host = "127.0.0.1";
        cfg_.task_diagnostics_interval = 3600;
        cfg_.task_backlog_flush_interval = 3600;
        cfg_.task_vacuum_interval = 3600;
        {
            asio::io_context io;
            tcp::acceptor port_picker{io, {tcp::v4(), 0}};
            cfg_.port = port_picker.local_endpoint().port();
        }

        db_ = std::make_unique<LogStore>(cfg_);
        db_->Open();
        db_->Initialize();

        db_ops_pool_ = std::make_unique<asio::thread_pool>(1u);
        reader_pool_ = std::make_unique<asio::thread_pool>(2u);

        backlog_ = std::make_unique<Backlog>(200);
        notifier_ = std::make_unique<LogNotifier>(cfg_.sse_limit);

        db_read_ = std::make_unique<LogReaderPool>(*db_, 2u);

        ctx_ = std::make_unique<ServerContext>(cfg_, *db_, *db_read_, *backlog_, *notifier_,
                                               asio::make_strand(db_ops_pool_->get_executor()),
                                               reader_pool_->get_executor());

        server_ = std::make_unique<Server>(*ctx_);

        server_finished_ = finished_.get_future();
        server_thread_ = std::thread{[this]() {
            try {
                server_->Run();
            } catch (...) {
                server_error_ = std::current_exception();
            }
            finished_.set_value();
        }};

        ASSERT_TRUE(test::WaitUntil(
            [&] {
                if (server_finished_.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
                    return true;
                try {
                    return http_req(cfg_.host, cfg_.port, http::verb::get, "/health").result() ==
                           http::status::ok;
                } catch (const boost::system::system_error&) {
                    return false;
                }
            },
            std::chrono::seconds{5}))
            << "Server did not become ready";
        ASSERT_NE(server_finished_.wait_for(std::chrono::seconds{0}), std::future_status::ready)
            << "Server exited during startup";
    }

    int FlushLogs(const std::vector<nlohmann::json>& logs) {
        for (const auto& row : logs) backlog_->Add(row);
        return asio::co_spawn(
                   ctx_->write_strand,
                   [this]() -> asio::awaitable<int> { co_return ctx_->FlushBacklog(); },
                   asio::use_future)
            .get();
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        EXPECT_EQ(server_error_, nullptr);
        server_.reset();

        // Destroy context first — strand destructor posts cleanup to the
        // executor, which must still be alive.
        ctx_.reset();
        if (db_read_) {
            db_read_->Close();
            db_read_.reset();
        }

        if (db_ops_pool_) {
            db_ops_pool_->stop();
            db_ops_pool_->join();
            db_ops_pool_.reset();
        }
        if (reader_pool_) {
            reader_pool_->stop();
            reader_pool_->join();
            reader_pool_.reset();
        }

        db_->Close();
        db_.reset();
    }

    test::TempDirectory directory_;
    Config cfg_{test::MakeConfig(directory_.path())};
    std::unique_ptr<LogStore> db_;
    std::unique_ptr<LogReaderPool> db_read_;
    std::unique_ptr<Backlog> backlog_;
    std::unique_ptr<LogNotifier> notifier_;
    std::unique_ptr<ServerContext> ctx_;
    std::unique_ptr<asio::thread_pool> db_ops_pool_;
    std::unique_ptr<asio::thread_pool> reader_pool_;
    std::unique_ptr<Server> server_;
    std::thread server_thread_;
    std::promise<void> finished_;
    std::future<void> server_finished_;
    std::exception_ptr server_error_;
};

TEST_F(ServerTest, RoutesEndpointsAndRejectsUnknownRoutesAndMethods) {
    struct Case {
        http::verb method;
        const char* target;
        http::status status;
        const char* key;
    };
    const Case cases[]{
        {http::verb::get, "/health", http::status::ok, "status"},
        {http::verb::get, "/settings", http::status::ok, "settings"},
        {http::verb::get, "/version", http::status::ok, "version"},
        {http::verb::get, "/schema", http::status::ok, "columns"},
        {http::verb::get, "/logs?limit=10&offset=0", http::status::bad_request, "error"},
        {http::verb::get, "/nonexistent", http::status::not_found, "error"},
        {http::verb::post, "/health", http::status::not_found, "error"},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.target);
        auto res = http_req(cfg_.host, cfg_.port, c.method, c.target);
        EXPECT_EQ(res.result(), c.status);
        auto body = nlohmann::json::parse(res.body());
        EXPECT_TRUE(body.contains(c.key));
        if (c.status == http::status::not_found) EXPECT_EQ(body["error"], "not found");
    }
}

// ── CORS preflight ──────────────────────────────────────────────────────────

TEST_F(ServerTest, OptionsReturnsNoContentWithCorsHeaders) {
    auto res = http_req("127.0.0.1", cfg_.port, http::verb::options, "/logs");
    EXPECT_EQ(res.result(), http::status::no_content);
    EXPECT_EQ(res[http::field::access_control_allow_origin], "*");
    EXPECT_TRUE(res[http::field::access_control_allow_methods].contains("GET"));
}

// ── Insert ──────────────────────────────────────────────────────────────────

TEST_F(ServerTest, KeepAliveRequestsEnqueueEachPayloadExactlyOnce) {
    const std::string payload =
        R"({"timestamp":"2024-01-01T00:00:00Z","message":"hello","level":"INFO"})";
    auto responses = http_req_keep_alive(cfg_.host, cfg_.port, http::verb::post, "/logs", payload,
                                         "application/json", 2);
    ASSERT_EQ(responses.size(), 2u);
    for (const auto& res : responses) {
        EXPECT_EQ(res.result(), http::status::ok);
        EXPECT_EQ(nlohmann::json::parse(res.body())["status"], "accepted");
    }
    EXPECT_EQ(backlog_->Flush(), (std::vector<nlohmann::json>(2, nlohmann::json::parse(payload))));
}

TEST_F(ServerTest, ShutdownLeavesBufferedLogsForFinalFlushAfterProducersStop) {
    auto res =
        http_req("127.0.0.1", cfg_.port, http::verb::post, "/logs",
                 R"({"timestamp":"2024-01-01T00:00:00Z","message":"shutdown","level":"INFO"})",
                 "application/json");
    EXPECT_EQ(res.result(), http::status::ok);

    // The flush interval is 3600s, so the entry must still be buffered.
    EXPECT_EQ(db_->EstimateLogRowCount(), 0);

    server_->Stop();
    server_thread_.join();

    // RunServer owns the final flush: harvesters may still enqueue during Stop().
    EXPECT_EQ(backlog_->Size(), 1u);
    EXPECT_EQ(db_->EstimateLogRowCount(), 0);
}

TEST_F(ServerTest, FatalBackgroundFailureIsRethrownAndRestoresBatch) {
    cfg_.migrations.push_back({2,
                               {"CREATE TRIGGER reject_log BEFORE INSERT ON TestLog "
                                "BEGIN SELECT RAISE(ABORT, 'injected background failure'); END"},
                               {"DROP TRIGGER reject_log"}});
    ASSERT_TRUE(db_->Rollout());
    for (int i = 0; i < 190; ++i) {
        backlog_->Add(
            {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "failure"}, {"level", "INFO"}});
    }
    EXPECT_EQ(server_finished_.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    server_->Stop();
    server_thread_.join();
    ASSERT_NE(server_error_, nullptr);
    EXPECT_THROW(std::rethrow_exception(server_error_), std::runtime_error);
    server_error_ = nullptr;  // the failure is expected in this test
    EXPECT_EQ(backlog_->Size(), 190u);
    EXPECT_EQ(db_->EstimateLogRowCount(), 0);
}

TEST_F(ServerTest, ShutdownWaitsForInFlightDatabaseWork) {
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    asio::post(ctx_->write_strand, [&] {
        entered.set_value();
        gate.wait();
    });
    EXPECT_EQ(entered_future.wait_for(std::chrono::seconds{2}), std::future_status::ready);
    // Fill to the watermark so the flush task queues behind the blocked writer.
    for (int i = 0; i < 190; ++i) {
        backlog_->Add(
            {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "pending"}, {"level", "INFO"}});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    server_->Stop();
    const auto before_release = server_finished_.wait_for(std::chrono::milliseconds{100});
    release.set_value();
    EXPECT_EQ(before_release, std::future_status::timeout);
    EXPECT_EQ(server_finished_.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    server_thread_.join();
    EXPECT_EQ(db_->EstimateLogRowCount(), 190);
}

// ── Query ───────────────────────────────────────────────────────────────────

TEST_F(ServerTest, QueryRoutingPreservesEncodedFiltersProjectionAndPagination) {
    ASSERT_EQ(
        db_->Insert({
            {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "first"}, {"level", "ERROR A"}},
            {{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "second"}, {"level", "INFO"}},
            {{"timestamp", "2024-01-01T00:00:02Z"}, {"message", "third"}, {"level", "ERROR A"}},
        }),
        3);
    auto res = http_req(cfg_.host, cfg_.port, http::verb::get,
                        "/logs?fields=message&limit=1&offset=1&level==ERROR%20A");
    EXPECT_EQ(res.result(), http::status::ok);
    const auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["total"], 2);
    EXPECT_EQ(body["results"], nlohmann::json::array({{{"message", "first"}}}));
}

// ── Stats endpoint ──────────────────────────────────────────────────────────

TEST_F(ServerTest, StatsRoutingReturnsPersistedValues) {
    // Insert some stats data directly.
    ASSERT_TRUE(db_->InsertActivityStats({.since = "2024-01-01T00:00:00Z",
                                          .until = "2024-01-01T00:01:00Z",
                                          .query_count = 10,
                                          .query_avg = 5}));
    ASSERT_TRUE(db_->InsertDatabaseStats({"2024-01-01T00:01:00Z", 100, 4096}));
    auto res = http_req(
        cfg_.host, cfg_.port, http::verb::get,
        "/stats?since=2024-01-01T00:00:00Z&until=2024-01-01T01:00:00Z"
        "&activity_stats_fields=query_count,query_avg&database_stats_fields=rows_count,db_size"
        "&ordering=asc");
    EXPECT_EQ(res.result(), http::status::ok);
    const auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["activities"]["fields"], (std::vector<std::string>{"query_count", "query_avg"}));
    EXPECT_EQ(body["activities"]["data"], nlohmann::json::array({{10, 5}}));
    EXPECT_EQ(body["database"]["fields"], (std::vector<std::string>{"rows_count", "db_size"}));
    EXPECT_EQ(body["database"]["data"], nlohmann::json::array({{100, 4096}}));
}

TEST_F(ServerTest, ShutdownCancelsIdleSSESubscriptions) {
    asio::io_context io;
    std::vector<tcp::socket> sockets;
    for (const auto* fields : {"*", "message,level"}) {
        sockets.emplace_back(io);
        auto& socket = sockets.back();
        socket.connect({asio::ip::make_address(cfg_.host), cfg_.port});
        http::request<http::empty_body> req{http::verb::get,
                                            fmt::format("/logs/sse?fields={}", fields), 11};
        req.set(http::field::host, cfg_.host);
        http::write(socket, req);
        // Read just the response header; the subscription then waits without new logs.
        beast::flat_buffer buffer;
        http::response_parser<http::empty_body> parser;
        http::read_header(socket, buffer, parser);
        const auto& res = parser.get();
        EXPECT_EQ(res.result(), http::status::ok);
        EXPECT_EQ(res[http::field::content_type], "text/event-stream");
        EXPECT_EQ(res[http::field::cache_control], "no-cache");
        EXPECT_TRUE(res.chunked());
    }
    ASSERT_TRUE(test::WaitUntil([&] { return notifier_->SubscriberCount() == 2; }));
    server_->Stop();
    EXPECT_EQ(server_finished_.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    server_thread_.join();
    EXPECT_EQ(notifier_->SubscriberCount(), 0u);
    EXPECT_EQ(metrics::MetricsRegistry::Instance().Gauge(metrics::kSseSession), 0);
    EXPECT_EQ(metrics::MetricsRegistry::Instance().Gauge(metrics::kHttpConnection), 0);
}

class PartitionedServerTest : public ServerTest {
   protected:
    void SetUp() override {
        cfg_.partition_interval = PartitionInterval::kDaily;
        cfg_.sse_limit = 2;
        cfg_.sse_debounce_ms = 10;
        ServerTest::SetUp();
    }
};

TEST_F(PartitionedServerTest, SSEUsesCommitOrderWindowAndNeverReplaysBeforeSubscription) {
    ASSERT_EQ(FlushLogs({{{"timestamp", "2023-12-01T00:00:00Z"},
                          {"message", "before subscription"},
                          {"level", "INFO"}}}),
              1);
    SSEClient client{cfg_, "/logs/sse?fields=message"};
    EXPECT_FALSE(client.AwaitEvents(1, std::chrono::milliseconds{100}));

    ASSERT_EQ(FlushLogs({
                  {{"timestamp", "2024-01-04T04:00:00Z"}, {"message", "first"}, {"level", "INFO"}},
                  {{"timestamp", "2024-01-04T01:00:00Z"}, {"message", "late"}, {"level", "INFO"}},
                  {{"timestamp", "2024-01-04T03:00:00Z"}, {"message", "third"}, {"level", "INFO"}},
                  {{"timestamp", "2024-01-04T02:00:00Z"}, {"message", "fourth"}, {"level", "INFO"}},
                  {{"timestamp", "2024-01-04T04:00:01Z"}, {"message", "fifth"}, {"level", "INFO"}},
              }),
              5);
    ASSERT_TRUE(client.AwaitEvents(1));
    EXPECT_EQ(client.events.front(),
              nlohmann::json::array({{{"message", "fifth"}}, {{"message", "fourth"}}}));

    ASSERT_EQ(FlushLogs({{{"timestamp", "2023-12-01T00:00:00Z"},
                          {"message", "older-timestamp"},
                          {"level", "INFO"}}}),
              1);
    ASSERT_TRUE(client.AwaitEvents(2));
    EXPECT_EQ(client.events.back(), nlohmann::json::array({{{"message", "older-timestamp"}}}));
    EXPECT_EQ(client.events.size(), 2u);
}

TEST_F(ServerTest, SSERejectsInvalidProjectionBeforeSendingEventStreamHeaders) {
    for (const auto* fields :
         {"", "unknown", "message,", ",message", "message,,level", "%20message"}) {
        SCOPED_TRACE(fields);
        const auto response = http_req(cfg_.host, cfg_.port, http::verb::get,
                                       fmt::format("/logs/sse?fields={}", fields));
        EXPECT_EQ(response.result(), http::status::bad_request);
        EXPECT_EQ(response[http::field::content_type], "application/json");
        EXPECT_TRUE(nlohmann::json::parse(response.body()).contains("error"));
    }
    EXPECT_EQ(notifier_->SubscriberCount(), 0u);
    EXPECT_EQ(metrics::MetricsRegistry::Instance().Gauge(metrics::kSseSession), 0);
}

TEST_F(ServerTest, SSESupportsAllFieldsAndIndependentProjectedSessionsWithoutReaderWorkers) {
    SSEClient all{cfg_};
    SSEClient star{cfg_, "/logs/sse?fields=*"};
    SSEClient projected{cfg_, "/logs/sse?fields=message,service"};
    ASSERT_EQ(notifier_->SubscriberCount(), 3u);

    // Occupy every reader worker and every connection. An SSE database query
    // would remain queued or block on a lease until the gate is released.
    std::latch entered{2};
    std::latch finished{2};
    std::promise<void> release;
    const auto gate = release.get_future().share();
    struct ReleaseGuard {
        std::promise<void>& release;
        std::latch& finished;
        ~ReleaseGuard() {
            release.set_value();
            EXPECT_TRUE(test::WaitUntil([&] { return finished.try_wait(); }));
        }
    } release_on_exit{release, finished};
    for (int i = 0; i < 2; ++i) {
        asio::post(reader_pool_->get_executor(), [&, gate] {
            db_read_->UseConnection([&](LogReader&) {
                entered.count_down();
                gate.wait();
            });
            finished.count_down();
        });
    }
    ASSERT_TRUE(test::WaitUntil([&] { return entered.try_wait(); }));

    ASSERT_EQ(
        FlushLogs(
            {{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "first"}, {"level", "INFO"}}}),
        1);
    ASSERT_TRUE(all.AwaitEvents(1));
    ASSERT_TRUE(star.AwaitEvents(1));
    ASSERT_TRUE(projected.AwaitEvents(1));
    const auto persisted = nlohmann::json::array({{{"id", 1},
                                                   {"timestamp", "2024-01-01T00:00:00Z"},
                                                   {"message", "first"},
                                                   {"level", "INFO"},
                                                   {"service", nullptr}}});
    EXPECT_EQ(all.events.front(), persisted);
    EXPECT_EQ(star.events.front(), persisted);
    EXPECT_EQ(projected.events.front(),
              nlohmann::json::array({{{"message", "first"}, {"service", nullptr}}}));

    all.Disconnect();
    ASSERT_EQ(
        FlushLogs(
            {{{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "second"}, {"level", "INFO"}}}),
        1);
    ASSERT_TRUE(star.AwaitEvents(2));
    ASSERT_TRUE(projected.AwaitEvents(2));
    EXPECT_EQ(projected.events.back(),
              nlohmann::json::array({{{"message", "second"}, {"service", nullptr}}}));

    ASSERT_EQ(
        FlushLogs(
            {{{"timestamp", "2024-01-01T00:00:02Z"}, {"message", "third"}, {"level", "INFO"}}}),
        1);
    ASSERT_TRUE(star.AwaitEvents(3));
    ASSERT_TRUE(projected.AwaitEvents(3));
    EXPECT_TRUE(test::WaitUntil([&] { return notifier_->SubscriberCount() == 2; }));
}

TEST_F(ServerTest, SSEDebounceCoalescesRapidCommitsBetweenSuccessfulPushes) {
    cfg_.sse_debounce_ms = 1000;
    SSEClient client{cfg_, "/logs/sse?fields=message"};
    ASSERT_EQ(
        FlushLogs(
            {{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "first"}, {"level", "INFO"}}}),
        1);
    ASSERT_TRUE(client.AwaitEvents(1));
    ASSERT_EQ(
        FlushLogs(
            {{{"timestamp", "2024-01-01T00:00:01Z"}, {"message", "second"}, {"level", "INFO"}}}),
        1);
    ASSERT_EQ(
        FlushLogs(
            {{{"timestamp", "2024-01-01T00:00:02Z"}, {"message", "third"}, {"level", "INFO"}}}),
        1);

    // A conservative fraction of the configured one-second interval avoids
    // depending on small scheduler or socket timing differences in CI.
    EXPECT_FALSE(client.AwaitEvents(2, std::chrono::milliseconds{100}));
    ASSERT_TRUE(client.AwaitEvents(2));
    EXPECT_EQ(client.events.back(),
              nlohmann::json::array({{{"message", "third"}}, {{"message", "second"}}}));
    EXPECT_EQ(client.events.size(), 2u);
}

TEST_F(ServerTest, SSEHeartbeatDetectsIdleClientDisconnectAndUnsubscribes) {
    SSEClient client{cfg_};
    ASSERT_TRUE(client.AwaitHeartbeat());
    EXPECT_TRUE(client.events.empty());
    client.Disconnect();
    // Force a write so disconnect cleanup need not wait for another heartbeat.
    for (const auto* timestamp : {"2024-01-01T00:00:00Z", "2024-01-01T00:00:01Z"}) {
        ASSERT_EQ(
            FlushLogs({{{"timestamp", timestamp}, {"message", "disconnect"}, {"level", "INFO"}}}),
            1);
    }
    EXPECT_TRUE(test::WaitUntil([&] { return notifier_->SubscriberCount() == 0; }));
    EXPECT_EQ(metrics::MetricsRegistry::Instance().Gauge(metrics::kSseSession), 0);
}

TEST_F(ServerTest, SSEHeartbeatIsNotDelayedByLongDebounceAfterData) {
    cfg_.sse_debounce_ms = 60000;
    SSEClient client{cfg_};
    ASSERT_EQ(
        FlushLogs(
            {{{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "first"}, {"level", "INFO"}}}),
        1);
    ASSERT_TRUE(client.AwaitEvents(1));
    ASSERT_TRUE(client.AwaitHeartbeat());
    EXPECT_EQ(client.events.size(), 1u);
}

// ── Handle connection error ─────────────────────────────────────────────────

TEST_F(ServerTest, DisconnectBeforeRequestLeavesServerResponsiveAndBalancesConnections) {
    asio::io_context io;
    tcp::socket socket{io};
    socket.connect({asio::ip::make_address(cfg_.host), cfg_.port});
    // Disconnect immediately without sending anything.
    socket.close();
    EXPECT_EQ(http_req(cfg_.host, cfg_.port, http::verb::get, "/health").result(),
              http::status::ok);
    EXPECT_TRUE(test::WaitUntil(
        [] { return metrics::MetricsRegistry::Instance().Gauge(metrics::kHttpConnection) == 0; }));
}
