#include "server.hpp"
#include "log.hpp"
#include "metrics.hpp"

#include <csignal>

#include "handlers/health.hpp"
#include "handlers/version_route.hpp"
#include "handlers/settings.hpp"
#include "handlers/schema.hpp"
#include "handlers/insert.hpp"
#include "handlers/query.hpp"
#include "handlers/sse.hpp"
#include "handlers/stats.hpp"

#include "tasks/diagnostics.hpp"
#include "tasks/flush_backlog.hpp"
#include "tasks/vacuum.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ip = asio::ip;

namespace loglite {

using namespace std::chrono_literals;

namespace {

void log_exception(std::exception_ptr eptr, std::string_view tag) {
    if (!eptr) return;
    try {
        std::rethrow_exception(eptr);
    } catch (const std::exception& e) {
        log::ERROR("{} {}", tag, e.what());
    } catch (...) {
        log::ERROR("{} unknown exception", tag);
    }
}

}  // namespace

Server::Server(ServerContext& ctx) : ctx_(ctx), pool_(1u), acceptor_(pool_) {}

void Server::Run() {
    auto& cfg = ctx_.config;
    auto ex = pool_.get_executor();

    // ── Bind TCP acceptor ─────────────────────────────────────────────────────
    ip::tcp::endpoint endpoint{ip::make_address(cfg.host), cfg.port};
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(ip::tcp::acceptor::reuse_address{true});
    acceptor_.bind(endpoint);
    acceptor_.listen();
    log::INFO("Listening on {}:{}", cfg.host, cfg.port);

    // ── Signal handling ───────────────────────────────────────────────────────
    asio::signal_set signals{ex, SIGINT, SIGTERM};
    signals.async_wait([this](const boost::system::error_code& ec, int signo) {
        if (!ec) {
            log::INFO("Received signal {}, shutting down gracefully", signo);
            Stop();
        }
    });

    // ── Fatal error handler for background tasks ──────────────────────────────
    auto on_task_error = [this](std::exception_ptr eptr) {
        log_exception(eptr, "Background task crashed — shutting down:");
        Stop();
    };

    // ── Background tasks ──────────────────────────────────────────────────────
    asio::co_spawn(ex, tasks::FlushBacklogTask(ctx_), on_task_error);
    asio::co_spawn(ex, tasks::VacuumTask(ctx_), on_task_error);
    asio::co_spawn(ex, tasks::DiagnosticsTask(ctx_), on_task_error);

    // ── Accept loop ───────────────────────────────────────────────────────────
    //
    // AcceptLoop is given its own completion handler that stops the pool once
    // the coroutine exits.  This guarantees AcceptLoop always co_returns
    // cleanly (processing the operation_aborted from acceptor_.close) before
    // pool_.stop() is called, which in turn ensures the accepted socket's
    // executor is still alive when the socket destructor runs — avoiding a
    // use-after-free that manifests on x86/GCC when pool_.stop() is called
    // immediately 🤦.
    asio::co_spawn(ex, AcceptLoop(acceptor_), [this](std::exception_ptr eptr) {
        log_exception(eptr, "AcceptLoop error:");
        pool_.stop();
    });

    pool_.join();  // blocks until AcceptLoop exits and calls pool_.stop()
}

void Server::Stop() {
    ctx_.RequestStop();
    boost::system::error_code ec;
    acceptor_.close(ec);
    // Closing the acceptor will eventually call pool_.stop().
}

asio::awaitable<void> Server::AcceptLoop(ip::tcp::acceptor& acceptor) {
    while (true) {
        auto [ec, socket] = co_await acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
        if (ec) {
            if (ec != asio::error::operation_aborted) log::ERROR("accept error: {}", ec.message());
            co_return;
        }

        beast::tcp_stream stream{std::move(socket)};
        stream.expires_after(60s);

        auto ex = co_await asio::this_coro::executor;
        asio::co_spawn(ex, HandleConnection(std::move(stream)),
                       [](std::exception_ptr eptr) { log_exception(eptr, "Connection error:"); });
    }
}

asio::awaitable<void> Server::HandleConnection(beast::tcp_stream stream) {
    metrics::GaugeGuard http_connection{metrics::kHttpConnection};

    beast::flat_buffer buf;
    auto& cfg = ctx_.config;

    for (;;) {
        http::request<http::string_body> req;

        try {
            co_await http::async_read(stream, buf, req, asio::use_awaitable);
        } catch (...) {
            co_return;
        }

        auto target = std::string(req.target());
        auto [path, _] = handlers::SplitURLTarget(target);
        auto method = req.method();

        // ── CORS preflight ────────────────────────────────────────────────────
        if (method == http::verb::options) {
            http::response<http::string_body> res{http::status::no_content, req.version()};
            res.set(http::field::access_control_allow_origin, cfg.allow_origin);
            res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
            res.set(http::field::access_control_allow_headers, "Content-Type");
            res.keep_alive(req.keep_alive());
            res.prepare_payload();
            try {
                co_await http::async_write(stream, res, asio::use_awaitable);
            } catch (...) {
                co_return;
            }
            if (res.need_eof()) {
                beast::error_code ec;
                stream.socket().shutdown(asio::ip::tcp::socket::shutdown_send, ec);
                co_return;
            }
            continue;
        }

        // ── Route dispatch ────────────────────────────────────────────────────
        if (path == "/logs/sse" && method == http::verb::get) {
            co_await handlers::HandleSSE(std::move(stream), std::move(req), ctx_);
            co_return;
        }

        http::response<http::string_body> res;

        if (path == "/logs" && method == http::verb::post) {
            res = handlers::HandleInsert(req, ctx_);
        } else if (path == "/logs" && method == http::verb::get) {
            res = co_await handlers::HandleQuery(req, ctx_);
        } else if (path == "/health" && method == http::verb::get) {
            res = co_await handlers::HandleHealth(req, ctx_);
        } else if (path == "/version" && method == http::verb::get) {
            res = handlers::HandleVersion(req, ctx_);
        } else if (path == "/stats" && method == http::verb::get) {
            res = co_await handlers::HandleStats(req, ctx_);
        } else if (path == "/settings" && method == http::verb::get) {
            res = handlers::HandleSettings(req, ctx_);
        } else if (path == "/schema" && method == http::verb::get) {
            res = handlers::HandleSchema(req, ctx_);
        } else {
            res = handlers::MakeFailResp(404, "not found", req, cfg.allow_origin);
        }

        try {
            co_await http::async_write(stream, res, asio::use_awaitable);
        } catch (...) {
            co_return;
        }

        if (res.need_eof()) {
            beast::error_code ec;
            stream.socket().shutdown(asio::ip::tcp::socket::shutdown_send, ec);
            co_return;
        }
    }
}

}  // namespace loglite
