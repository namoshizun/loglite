#ifndef LOGLITE_HANDLERS_SSE_HPP_
#define LOGLITE_HANDLERS_SSE_HPP_

#include "common.hpp"
#include "../access.hpp"
#include "../log.hpp"
#include "../metrics.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http/chunk_encode.hpp>

#include <algorithm>
#include <chrono>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

namespace loglite::handlers {

using namespace std::chrono_literals;

inline constexpr size_t kMaxSseSubscribers = 128;
inline constexpr auto kSseWriteTimeout = 10s;

inline asio::awaitable<void> HandleSSE(beast::tcp_stream& stream, Request req, HttpAccess http) {
    const auto& cfg = http.config;
    auto fields = req.ListParam("fields").value_or(std::vector<std::string>{"*"});
    std::string field_error;
    try {
        fields = ResolveLogFields(http.schema.columns(), fields);
    } catch (const std::runtime_error& error) {
        field_error = error.what();
    }
    if (!field_error.empty()) {
        auto response = MakeFailResp(400, field_error, req, cfg.allow_origin);
        co_await http::async_write(stream, response, asio::as_tuple(asio::use_awaitable));
        co_return;
    }

    auto timer = std::make_shared<asio::steady_timer>(co_await asio::this_coro::executor);
    struct SubscriptionGuard {
        LogNotifier& _notifier;
        std::shared_ptr<asio::steady_timer> _timer;
        ~SubscriptionGuard() { _notifier.Unsubscribe(_timer); }
    } unsubscribe{http.live, timer};

    if (http.live.SubscriberCount() >= kMaxSseSubscribers) {
        auto response = MakeFailResp(503, "too many live subscribers", req, cfg.allow_origin);
        co_await http::async_write(stream, response, asio::as_tuple(asio::use_awaitable));
        co_return;
    }

    stream.expires_after(kSseWriteTimeout);

    http::response<http::empty_body> response{http::status::ok, req.version()};
    response.set(http::field::content_type, "text/event-stream");
    response.set(http::field::cache_control, "no-cache");
    response.set(http::field::connection, "keep-alive");
    response.set("X-Accel-Buffering", "no");
    response.set(http::field::access_control_allow_origin, cfg.allow_origin);
    response.chunked(true);
    http::response_serializer<http::empty_body> serializer{response};
    const auto [header_error, header_bytes] =
        co_await http::async_write_header(stream, serializer, asio::as_tuple(asio::use_awaitable));

    if (header_error) co_return;

    metrics::GaugeGuard sse_session{metrics::kSseSession};

    auto last_write = std::chrono::steady_clock::now();
    auto next_push = std::chrono::steady_clock::time_point{};

    const auto subscriber_id = reinterpret_cast<uintptr_t>(timer.get());
    log::INFO("SSE subscriber {} connected (subscribers={})", subscriber_id,
              http.live.SubscriberCount());

    auto send = [&](std::string_view payload) -> asio::awaitable<bool> {
        stream.expires_after(kSseWriteTimeout);
        const auto [error, bytes] = co_await asio::async_write(
            stream, http::make_chunk(asio::buffer(payload)), asio::as_tuple(asio::use_awaitable));
        stream.expires_never();
        co_return !error;
    };

    const auto debounce = cfg.sse_debounce_ms * 1ms;
    const auto heartbeat_interval = cfg.sse_heartbeat_ms * 1ms;
    uint64_t cursor = http.live.Subscribe(timer);

    while (!http.StopRequested()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_push) {
            if (auto rows = http.live.Since(cursor); !rows.empty()) {
                auto payload = nlohmann::json::array();
                for (const auto& row : rows | std::views::reverse) {
                    auto projected = nlohmann::json::object();
                    for (const auto& field : fields) projected[field] = row->at(field);
                    payload.push_back(std::move(projected));
                }

                const std::string event = "data: " + payload.dump() + "\r\n\r\n";
                if (!co_await send(event)) break;

                last_write = std::chrono::steady_clock::now();
                next_push = last_write + debounce;
                log::DEBUG("SSE {} pushed {} log(s)", subscriber_id, rows.size());

                // Recheck publications that arrived while a write was in flight.
                continue;
            }
        }

        if (now - last_write >= heartbeat_interval) {
            if (!co_await send(":\r\n\r\n")) break;
            last_write = std::chrono::steady_clock::now();
            continue;
        }

        const auto next_beat = last_write + heartbeat_interval;
        timer->expires_at(now < next_push ? std::min(next_push, next_beat) : next_beat);
        co_await timer->async_wait(asio::as_tuple(asio::use_awaitable));
    }

    co_await asio::async_write(stream, http::make_chunk_last(),
                               asio::as_tuple(asio::use_awaitable));
    log::INFO("SSE subscriber {} disconnected", subscriber_id);
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_SSE_HPP_
