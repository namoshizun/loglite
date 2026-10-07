#include "server.hpp"
#include "log.hpp"
#include "metrics.hpp"

#include <csignal>

#include "handlers/router.hpp"
#include "handlers/sse.hpp"

#include "tasks/diagnostics.hpp"
#include "tasks/flush_backlog.hpp"
#include "tasks/vacuum.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <array>
#include <chrono>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ip = asio::ip;

namespace loglite {

using namespace std::chrono_literals;

namespace {

constexpr auto kHttpIdleTimeout = 60s;

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
    asio::co_spawn(
        ex,
        [this, &cfg]() -> asio::awaitable<void> {
            ip::tcp::endpoint endpoint{ip::make_address(cfg.host), cfg.port};
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(ip::tcp::acceptor::reuse_address{true});
            acceptor_.bind(endpoint);
            acceptor_.listen();
            co_return;
        },
        asio::use_future)
        .get();
    log::INFO("Listening on {}:{}", cfg.host, cfg.port);

    // ── Signal handling ───────────────────────────────────────────────────────
    asio::signal_set signals{ex, SIGINT, SIGTERM};
    signals.async_wait([this](const boost::system::error_code& ec, int signo) {
        if (!ec) {
            log::INFO("Received signal {}, shutting down gracefully", signo);
            Stop();
        }
    });

    // ── Background tasks ──────────────────────────────────────────────────────
    std::array background_tasks{tasks::FlushBacklogTask(ctx_), tasks::VacuumTask(ctx_),
                                tasks::DiagnosticsTask(ctx_)};
    pending_tasks_ = background_tasks.size() + 1;  // plus the accept loop
    for (auto& task : background_tasks) {
        asio::co_spawn(ex, std::move(task),
                       [this](std::exception_ptr eptr) { OnTaskCompleted(eptr); });
    }

    // ── Accept loop ───────────────────────────────────────────────────────────
    //
    // Wait for the accept loop, background tasks, and connections to finish
    // before stopping the pool. This keeps coroutine captures alive until their
    // in-flight DB work completes and ensures accepted sockets are destroyed
    // while their executor is still alive — avoiding a
    // use-after-free that manifests on x86/GCC when pool_.stop() is called
    // immediately 🤦.
    asio::co_spawn(ex, AcceptLoop(acceptor_), [this](std::exception_ptr eptr) {
        Stop();
        OnTaskCompleted(eptr);
    });

    if (ctx_.StopRequested()) Stop();

    pool_.join();
    if (failure_) std::rethrow_exception(failure_);
}

void Server::Stop() {
    ctx_.stopping.store(true, std::memory_order_release);
    asio::dispatch(pool_.get_executor(), [this] {
        ctx_.RequestStop();
        boost::system::error_code ec;
        acceptor_.close(ec);
        for (auto& stream : connections_) stream.socket().close(ec);
    });
}

void Server::OnTaskCompleted(std::exception_ptr error) {
    if (error) {
        if (!failure_) failure_ = error;
        log_exception(error, "Server task crashed — shutting down:");
        Stop();
    }
    if (--pending_tasks_ == 0) pool_.stop();
}

asio::awaitable<void> Server::AcceptLoop(ip::tcp::acceptor& acceptor) {
    while (true) {
        auto [ec, socket] = co_await acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
        if (ec) {
            if (ec != asio::error::operation_aborted && !ctx_.StopRequested())
                throw boost::system::system_error(ec);
            co_return;
        }

        auto connection = connections_.emplace(connections_.end(), std::move(socket));
        ++pending_tasks_;

        auto ex = co_await asio::this_coro::executor;
        asio::co_spawn(ex, HandleConnection(*connection),
                       [this, connection](std::exception_ptr eptr) {
                           log_exception(eptr, "Connection error:");
                           connections_.erase(connection);
                           OnTaskCompleted(nullptr);
                       });
    }
}

asio::awaitable<void> Server::HandleConnection(beast::tcp_stream& stream) {
    metrics::GaugeGuard http_connection{metrics::kHttpConnection};

    beast::flat_buffer buf;
    auto& cfg = ctx_.config;

    while (!ctx_.StopRequested()) {
        // Per-request idle timeout: re-arm each keep-alive iteration (not once at accept).
        stream.expires_after(kHttpIdleTimeout);

        http::request<http::string_body> raw;
        try {
            co_await http::async_read(stream, buf, raw, asio::use_awaitable);
        } catch (...) {
            co_return;
        }

        handlers::Request req{std::move(raw)};
        const auto method = req.method();

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
        if (req.path() == "/logs/sse" && method == http::verb::get) {
            co_await handlers::HandleSSE(stream, std::move(req), ctx_);
            co_return;
        }

        auto routed = co_await handlers::Dispatch(req, ctx_);
        http::response<http::string_body> res =
            routed ? std::move(*routed)
                   : handlers::MakeFailResp(404, "not found", req, cfg.allow_origin);

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
