#pragma once

#include "common.hpp"
#include "../globals.hpp"

namespace loglite::handlers {

template <class Body>
http::response<http::string_body> handle_health(const http::request<Body>& req,
                                                ServerContext& ctx) {
    bool ok_flag = ctx.db.ping();
    return make_json_response(ok_flag ? http::status::ok : http::status::service_unavailable,
                              {{"status", ok_flag ? "ok" : "error"}}, req, ctx.config.allow_origin);
}

}  // namespace loglite::handlers
