#ifndef LOGLITE_HANDLERS_HEALTH_HPP_
#define LOGLITE_HANDLERS_HEALTH_HPP_

#include "common.hpp"
#include "../globals.hpp"

namespace loglite::handlers {

template <class Body>
http::response<http::string_body> HandleHealth(const http::request<Body>& req, ServerContext& ctx) {
    bool ok_flag = ctx.db.Ping();
    if (ok_flag) {
        return MakeOKResp({{"status", "ok"}}, req, ctx.config.allow_origin);
    } else {
        return MakeNotAvailableResp({{"status", "error"}}, req, ctx.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_HEALTH_HPP_
