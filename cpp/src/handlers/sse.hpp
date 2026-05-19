#ifndef LOGLITE_HANDLERS_SSE_HPP_
#define LOGLITE_HANDLERS_SSE_HPP_

#include "common.hpp"
#include "../globals.hpp"
#include "../log.hpp"
#include "../metrics.hpp"
#include "../utils.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http/chunk_encode.hpp>

#include <chrono>
#include <format>
#include <sstream>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

namespace loglite::handlers {

using namespace std::chrono_literals;

// ── SSE handler ────────────────────────────────────────────────────────────────
//
// Long-running coroutine that owns the TCP stream.  It:
//   1. Sends HTTP 200 headers with Transfer-Encoding: chunked.
//   2. Registers a subscription timer with LogNotifier.
//   3. Arms the timer to expire after sse_debounce_ms.
//      - If notify() cancels the timer early → new logs available.
//      - If timer fires normally → check for any logs missed during processing.
//   4. Queries DB for id > pushed_id AND id <= current_id and sends SSE chunk.
//   5. On write error (client disconnect), returns.

inline asio::awaitable<void> HandleSSE(beast::tcp_stream stream,
                                       http::request<http::string_body> req, ServerContext& ctx) {
    auto ex = co_await asio::this_coro::executor;
    auto& cfg = ctx.config;
    auto origin = cfg.allow_origin;
    auto debounce = cfg.sse_debounce_ms * 1ms;

    // ── Parse fields param ────────────────────────────────────────────────────
    auto [path, qs] = SplitURLTarget(req.target());
    auto params = ParseQueryString(qs);
    std::vector<std::string> fields;
    if (auto it = params.find("fields"); it != params.end() && it->second != "*") {
        for (auto sv : std::views::split(it->second, ','))
            fields.emplace_back(sv.begin(), sv.end());
    } else {
        fields = {"*"};
    }

    // ── Send response headers ─────────────────────────────────────────────────
    stream.expires_never();
    http::response<http::empty_body> res{http::status::ok, req.version()};
    res.set(http::field::content_type, "text/event-stream");
    res.set(http::field::cache_control, "no-cache");
    res.set(http::field::access_control_allow_origin, origin);
    res.chunked(true);

    http::response_serializer<http::empty_body> sr{res};
    try {
        co_await http::async_write_header(stream, sr, asio::use_awaitable);
    } catch (...) {
        co_return;
    }

    metrics::GaugeGuard sse_session{metrics::kSseSession};

    // ── Subscribe ─────────────────────────────────────────────────────────────
    auto sub = ctx.notifier.Subscribe(ex);
    auto unsub = std::unique_ptr<LogNotifier, std::function<void(LogNotifier*)>>(
        &ctx.notifier, [&sub](LogNotifier* n) { n->Unsubscribe(sub); });

    int64_t pushed_id = ctx.notifier.GetLastId();
    auto last_push_tp = std::chrono::steady_clock::time_point{};

    auto subscriber_id = reinterpret_cast<uintptr_t>(sub.get());
    log::info(std::format("SSE subscriber {} connected (subscribers={})", subscriber_id,
                          ctx.notifier.SubscriberCount()));

    // ── Event loop ────────────────────────────────────────────────────────────
    while (true) {
        // Arm the subscription timer.  notify() cancels it early when new logs arrive.
        sub->timer->expires_after(debounce);
        co_await sub->timer->async_wait(asio::as_tuple(asio::use_awaitable));
        // ec == success        → timer fired (timeout, still check for anything missed)
        // ec == operation_aborted → cancelled by notify() (new logs available)

        int64_t current_id = ctx.notifier.GetLastId();
        if (current_id <= pushed_id) continue;  // nothing new

        // Apply debounce: do not push more than once per debounce window.
        auto now = std::chrono::steady_clock::now();
        if (last_push_tp.time_since_epoch().count() != 0 && (now - last_push_tp) < debounce)
            continue;

        // ── Query new logs ────────────────────────────────────────────────────
        std::vector<QueryFilter> id_filters{
            {"id", ">", pushed_id},
            {"id", "<=", current_id},
        };
        PaginatedQueryResult result;
        try {
            result = ctx.db_read.with_connection(
                [&](ReaderDatabase& r) { return r.Query(fields, id_filters, cfg.sse_limit, 0); });
        } catch (const std::exception& e) {
            log::error(std::format("SSE query error: {}", e.what()));
            continue;
        }

        if (result.results.empty()) {
            pushed_id = current_id;
            continue;
        }

        // ── Write SSE event chunk ─────────────────────────────────────────────
        std::ostringstream payload;
        payload << "data: [";
        for (size_t i = 0; i < result.results.size(); ++i) {
            if (i != 0) payload << ',';
            payload << result.results[i];
        }
        payload << "]\r\n\r\n";
        std::string event = std::move(payload).str();
        auto chunk = http::make_chunk(net::buffer(event));

        try {
            co_await net::async_write(stream, chunk, asio::use_awaitable);
        } catch (...) {
            break;  // client disconnected
        }

        pushed_id = current_id;
        last_push_tp = now;

        if (cfg.debug)
            log::debug(
                std::format("SSE {} pushed {} log(s)", subscriber_id, result.results.size()));
    }

    // Send chunked terminator (best-effort; client may already be gone).
    try {
        co_await net::async_write(stream, http::make_chunk_last(), asio::use_awaitable);
    } catch (...) {
    }

    log::info(std::format("SSE subscriber {} disconnected (subscribers={})", subscriber_id,
                          ctx.notifier.SubscriberCount()));
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_SSE_HPP_
