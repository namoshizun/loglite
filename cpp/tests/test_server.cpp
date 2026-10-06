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
        notifier_ = std::make_unique<LogNotifier>();
        notifier_->Notify(db_->GetCommittedLogId());

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

TEST_F(PartitionedServerTest, SSESendsNewestPageInQueryOrder) {
    auto insert_and_notify = [&](std::vector<nlohmann::json> logs) {
        const auto last_id = asio::co_spawn(
                                 ctx_->write_strand,
                                 [&]() -> asio::awaitable<int64_t> {
                                     db_->Insert(logs);
                                     co_return db_->GetCommittedLogId();
                                 },
                                 asio::use_future)
                                 .get();
        notifier_->Notify(last_id);
    };
    insert_and_notify({{{"timestamp", "2024-01-05T00:00:00Z"},
                        {"message", "before subscription"},
                        {"level", "INFO"}}});

    asio::io_context io;
    tcp::socket socket{io};
    socket.connect({asio::ip::make_address(cfg_.host), cfg_.port});
    http::request<http::empty_body> req{http::verb::get, "/logs/sse?fields=message", 11};
    req.set(http::field::host, cfg_.host);
    http::write(socket, req);
    beast::flat_buffer buffer;
    http::response_parser<http::empty_body> parser;
    http::read_header(socket, buffer, parser);
    ASSERT_EQ(parser.get().result(), http::status::ok);
    ASSERT_TRUE(test::WaitUntil([&] { return notifier_->SubscriberCount() == 1; }));
    socket.non_blocking(true);

    std::string pending = beast::buffers_to_string(buffer.data());
    std::vector<nlohmann::json> events;
    auto await_events = [&](size_t expected) {
        return test::WaitUntil(
            [&] {
                char bytes[4096];
                boost::system::error_code ec;
                const auto count = socket.read_some(asio::buffer(bytes), ec);
                if (ec != asio::error::would_block && ec != asio::error::try_again && ec) {
                    throw boost::system::system_error(ec);
                }
                pending.append(bytes, count);
                for (;;) {
                    const auto start = pending.find("data: ");
                    if (start == std::string::npos) break;
                    const auto end = pending.find("\r\n\r\n", start);
                    if (end == std::string::npos) break;
                    events.push_back(
                        nlohmann::json::parse(pending.substr(start + 6, end - start - 6)));
                    pending.erase(0, end + 4);
                }
                return events.size() >= expected;
            },
            std::chrono::seconds{3});
    };

    insert_and_notify({
        {{"timestamp", "2024-01-04T00:00:00Z"}, {"message", "first"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-01T00:00:00Z"}, {"message", "late"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-03T00:00:00Z"}, {"message", "third"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-02T00:00:00Z"}, {"message", "fourth"}, {"level", "INFO"}},
        {{"timestamp", "2024-01-04T00:00:01Z"}, {"message", "fifth"}, {"level", "INFO"}},
    });
    // Same order as GET /logs. sse_limit keeps the newest page and the cursor
    // advances to the high watermark, so the older rows of this burst are not replayed.
    ASSERT_TRUE(await_events(1));
    EXPECT_EQ(events, (std::vector<nlohmann::json>{
                          nlohmann::json::array({{{"message", "fifth"}}, {{"message", "first"}}}),
                      }));

    insert_and_notify({
        {{"timestamp", "2023-12-01T00:00:00Z"}, {"message", "sixth"}, {"level", "INFO"}},
        {{"timestamp", "2023-12-02T00:00:00Z"}, {"message", "seventh"}, {"level", "INFO"}},
    });
    ASSERT_TRUE(await_events(2));
    EXPECT_EQ(events.back(),
              nlohmann::json::array({{{"message", "seventh"}}, {{"message", "sixth"}}}));

    insert_and_notify(
        {{{"timestamp", "2023-11-01T00:00:00Z"}, {"message", "later arrival"}, {"level", "INFO"}}});
    ASSERT_TRUE(await_events(3));
    EXPECT_EQ(events.back(), nlohmann::json::array({{{"message", "later arrival"}}}));
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
