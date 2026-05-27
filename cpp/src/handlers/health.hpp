#ifndef LOGLITE_HANDLERS_HEALTH_HPP_
#define LOGLITE_HANDLERS_HEALTH_HPP_

#include "common.hpp"
#include "../context.hpp"

namespace loglite::handlers {

template <class Body>
asio::awaitable<http::response<http::string_body>> HandleHealth(const http::request<Body>& req,
                                                                ServerContext& ctx) {
    bool ok_flag = co_await ctx.db_read.AsyncUseConnection(
        ctx.reader_executor, [&](ReaderDatabase& r) { return r.Ping(); });
    if (ok_flag) {
        co_return MakeOKResp({{"status", "ok"}}, req, ctx.config.allow_origin);
    } else {
        co_return MakeNotAvailableResp({{"status", "error"}}, req, ctx.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_HEALTH_HPP_
