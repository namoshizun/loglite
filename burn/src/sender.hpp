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
};

struct RunControl {
    std::atomic<bool> stop{false};
    std::atomic<unsigned> senders_left{0};
    asio::io_context* ioc{nullptr};
    std::shared_ptr<Stats> stats;

    explicit RunControl(asio::io_context& ioc_ref)
        : ioc(&ioc_ref), stats(std::make_shared<Stats>()) {}

    void SenderFinished() {
        if (senders_left.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ioc->stop();
        }
    }
};

inline asio::awaitable<void> ConnectStream(beast::tcp_stream& stream, const Endpoint& ep) {
    auto ex = co_await asio::this_coro::executor;
    tcp::resolver resolver{ex};
    auto results = co_await resolver.async_resolve(ep.host, ep.port, asio::use_awaitable);
    co_await asio::async_connect(stream.socket(), results, asio::use_awaitable);
}

inline asio::awaitable<bool> PostLog(beast::tcp_stream& stream, beast::flat_buffer& buf,
                                     const Endpoint& ep, std::string_view body, bool keep_alive) {
    http::request<http::string_body> req{http::verb::post, "/logs", 11};
    req.set(http::field::host, ep.host);
    req.set(http::field::user_agent, "loglite-burn");
    req.set(http::field::content_type, "application/json");
    req.keep_alive(keep_alive);
    req.body() = std::string(body);
    req.prepare_payload();

    try {
        co_await http::async_write(stream, req, asio::use_awaitable);

        http::response<http::string_body> res;
        co_await http::async_read(stream, buf, res, asio::use_awaitable);

        if (res.result() != http::status::ok) {
            co_return false;
        }
        auto parsed = nlohmann::json::parse(res.body());
        co_return parsed.value("status", std::string{}) == "accepted";
    } catch (...) {
        co_return false;
    }
}

inline asio::awaitable<void> SenderLoop(unsigned sender_id, const Config& cfg,
                                        std::shared_ptr<const SchemaPlan> plan,
                                        std::shared_ptr<RunControl> ctrl) {
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

    const auto interval = cfg.per_sender_qps > 0.0
                              ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::duration<double>(1.0 / cfg.per_sender_qps))
                              : std::chrono::nanoseconds::max();

    asio::steady_timer pace{ex};

    while (!ctrl->stop.load(std::memory_order_acquire)) {
        pace.expires_after(interval);
        boost::system::error_code ec;
        co_await pace.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (ec == asio::error::operation_aborted) {
            break;
        }
        if (ctrl->stop.load(std::memory_order_acquire)) {
            break;
        }

        const std::string payload = BuildLogRecord(*plan, cfg, rng).dump();
        bool ok = co_await PostLog(stream, buf, cfg.endpoint, payload, true);
        if (!ok) {
            beast::error_code close_ec;
            stream.socket().close(close_ec);
            buf.consume(buf.size());
            try {
                co_await ConnectStream(stream, cfg.endpoint);
                ok = co_await PostLog(stream, buf, cfg.endpoint, payload, true);
            } catch (...) {
                ok = false;
            }
        }

        if (ok) {
            ctrl->stats->ok.fetch_add(1, std::memory_order_relaxed);
        } else {
            ctrl->stats->fail.fetch_add(1, std::memory_order_relaxed);
        }
    }

    beast::error_code ec;
    stream.socket().close(ec);
}

}  // namespace burn

#endif
