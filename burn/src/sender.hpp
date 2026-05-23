#ifndef BURN_SENDER_HPP_
#define BURN_SENDER_HPP_

#include "config.hpp"
#include "schema.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace burn {

struct Stats {
    std::atomic<uint64_t> ok{0};
    std::atomic<uint64_t> fail{0};

    void Record(bool success) noexcept {
        (success ? ok : fail).fetch_add(1, std::memory_order_relaxed);
    }
};

struct RunControl {
    std::atomic<bool> stop{false};
    std::atomic<unsigned> senders_left{0};
    asio::io_context& ioc;
    std::shared_ptr<Stats> stats;

    explicit RunControl(asio::io_context& ioc_ref)
        : ioc(ioc_ref), stats(std::make_shared<Stats>()) {}

    void SenderFinished() {
        if (senders_left.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ioc.stop();
        }
    }
};

inline asio::awaitable<void> ConnectStream(beast::tcp_stream& stream, const Endpoint& ep) {
    auto ex = co_await asio::this_coro::executor;
    tcp::resolver resolver{ex};
    auto results = co_await resolver.async_resolve(ep.host, ep.port, asio::use_awaitable);
    co_await asio::async_connect(stream.socket(), results, asio::use_awaitable);
}

[[nodiscard]] inline asio::awaitable<bool> PostLog(beast::tcp_stream& stream,
                                                   beast::flat_buffer& buf, const Endpoint& ep,
                                                   std::string_view body) {
    http::request<http::string_body> req{http::verb::post, "/logs", 11};
    req.set(http::field::host, ep.host);
    req.set(http::field::user_agent, "loglite-burn");
    req.set(http::field::content_type, "application/json");
    req.keep_alive(true);
    req.body() = std::string{body};
    req.prepare_payload();

    if (auto [wec, _] =
            co_await http::async_write(stream, req, asio::as_tuple(asio::use_awaitable));
        wec) {
        co_return false;
    }

    http::response<http::string_body> res;
    if (auto [rec, __] =
            co_await http::async_read(stream, buf, res, asio::as_tuple(asio::use_awaitable));
        rec) {
        co_return false;
    }

    if (res.result() != http::status::ok) {
        co_return false;
    }

    try {
        const auto parsed = nlohmann::json::parse(res.body());
        co_return parsed.value("status", std::string{}) == "accepted";
    } catch (...) {
        co_return false;
    }
}

[[nodiscard]] inline asio::awaitable<bool> PostLogWithReconnect(beast::tcp_stream& stream,
                                                                beast::flat_buffer& buf,
                                                                const Endpoint& ep,
                                                                std::string_view body) {
    if (co_await PostLog(stream, buf, ep, body)) {
        co_return true;
    }

    beast::error_code close_ec;
    stream.socket().close(close_ec);
    buf.consume(buf.size());

    try {
        co_await ConnectStream(stream, ep);
    } catch (...) {
        co_return false;
    }
    co_return co_await PostLog(stream, buf, ep, body);
}

inline asio::awaitable<void> SenderLoop(unsigned sender_id, const Config& cfg,
                                        const SchemaPlan plan, std::shared_ptr<RunControl> ctrl) {
    struct DoneGuard {
        std::shared_ptr<RunControl> ctrl;
        ~DoneGuard() { ctrl->SenderFinished(); }
    } done{ctrl};

    std::random_device rd;
    std::mt19937_64 rng{rd() ^ (static_cast<uint64_t>(sender_id) + 1)};

    auto ex = co_await asio::this_coro::executor;
    beast::tcp_stream stream{ex};
    stream.expires_never();
    beast::flat_buffer buf;

    co_await ConnectStream(stream, cfg.endpoint);

    const auto interval = SenderPaceInterval(cfg.per_sender_qps);
    asio::steady_timer pace{ex};

    while (!ctrl->stop.load(std::memory_order_acquire)) {
        pace.expires_after(interval);
        if (auto [ec] = co_await pace.async_wait(asio::as_tuple(asio::use_awaitable)); ec) {
            break;
        }
        if (ctrl->stop.load(std::memory_order_acquire)) {
            break;
        }

        const std::string payload = BuildLogRecord(plan, cfg, rng).dump();
        const bool ok = co_await PostLogWithReconnect(stream, buf, cfg.endpoint, payload);
        ctrl->stats->Record(ok);
    }

    beast::error_code ec;
    stream.socket().close(ec);
}

}  // namespace burn

#endif
