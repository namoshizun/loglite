#include "server.hpp"
#include "log.hpp"

#include "handlers/health.hpp"
#include "handlers/insert.hpp"
#include "handlers/query.hpp"
#include "handlers/sse.hpp"

#include "tasks/diagnostics.hpp"
#include "tasks/flush_backlog.hpp"
#include "tasks/vacuum.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <format>

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
namespace ip    = asio::ip;

namespace loglite {

namespace {

// Log and swallow exceptions from detached coroutines.
void on_coro_error(std::exception_ptr eptr) {
    if (!eptr) return;
    try { std::rethrow_exception(eptr); }
    catch (const std::exception& e) { log::error(std::format("Coroutine error: {}", e.what())); }
    catch (...)                      { log::error("Coroutine: unknown exception"); }
}

} // namespace

Server::Server(ServerContext& ctx, unsigned int thread_count)
    : ctx_(ctx),
      pool_(thread_count > 0 ? thread_count : std::max(1u, std::thread::hardware_concurrency())),
      acceptor_(pool_)
{}

void Server::run() {
    auto& cfg = ctx_.config;
    auto  ex  = pool_.get_executor();

    // ── Bind TCP acceptor ─────────────────────────────────────────────────────
    ip::tcp::endpoint endpoint{ip::make_address(cfg.host), cfg.port};
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(ip::tcp::acceptor::reuse_address{true});
    acceptor_.bind(endpoint);
    acceptor_.listen();
    log::info(std::format("Listening on {}:{}", cfg.host, cfg.port));

    // ── Background tasks ──────────────────────────────────────────────────────
    asio::co_spawn(ex, tasks::flush_backlog_task(ctx_), on_coro_error);
    asio::co_spawn(ex, tasks::vacuum_task(ctx_),        on_coro_error);
    asio::co_spawn(ex, tasks::diagnostics_task(ctx_),   on_coro_error);

    // ── Accept loop ───────────────────────────────────────────────────────────
    asio::co_spawn(ex, accept_loop(acceptor_), on_coro_error);

    pool_.join(); // blocks until stop() is called
}

void Server::stop() {
    acceptor_.close();
    pool_.stop();
}

asio::awaitable<void> Server::accept_loop(ip::tcp::acceptor& acceptor) {
    while (true) {
        auto [ec, socket] = co_await acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
        if (ec) {
            if (ec != asio::error::operation_aborted)
                log::error(std::format("accept error: {}", ec.message()));
            co_return;
        }

        beast::tcp_stream stream{std::move(socket)};
        stream.expires_after(std::chrono::seconds(60));

        auto ex = co_await asio::this_coro::executor;
        asio::co_spawn(ex,
            handle_connection(std::move(stream)),
            on_coro_error);
    }
}

asio::awaitable<void> Server::handle_connection(beast::tcp_stream stream) {
    beast::flat_buffer buf;
    http::request<http::string_body> req;

    try {
        co_await http::async_read(stream, buf, req, asio::use_awaitable);
    } catch (...) { co_return; }

    auto target = std::string(req.target());
    auto [path, _] = handlers::split_target(target);
    auto method    = req.method();
    auto& cfg      = ctx_.config;

    // ── CORS preflight ────────────────────────────────────────────────────────
    if (method == http::verb::options) {
        http::response<http::string_body> res{http::status::no_content, req.version()};
        res.set(http::field::access_control_allow_origin,  cfg.allow_origin);
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type");
        res.prepare_payload();
        co_await http::async_write(stream, res, asio::use_awaitable);
        co_return;
    }

    // ── Route dispatch ────────────────────────────────────────────────────────
    if (path == "/logs/sse" && method == http::verb::get) {
        // Long-lived; hands off the stream.
        co_await handlers::handle_sse(std::move(stream), std::move(req), ctx_);
        co_return;
    }

    http::response<http::string_body> res;

    if (path == "/logs" && method == http::verb::post) {
        res = handlers::handle_insert(req, ctx_);
    } else if (path == "/logs" && method == http::verb::get) {
        res = handlers::handle_query(req, ctx_);
    } else if (path == "/health" && method == http::verb::get) {
        res = handlers::handle_health(req, ctx_);
    } else {
        res = handlers::fail(404, "not found", req, cfg.allow_origin);
    }

    try {
        co_await http::async_write(stream, res, asio::use_awaitable);
    } catch (...) {}

    beast::error_code ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_send, ec);
}

} // namespace loglite
