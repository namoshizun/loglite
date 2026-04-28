#ifndef LOGLITE_HANDLERS_INSERT_HPP_
#define LOGLITE_HANDLERS_INSERT_HPP_

#include "common.hpp"
#include "../globals.hpp"
#include "../log.hpp"

namespace loglite::handlers {

template <class Body>
http::response<http::string_body> HandleInsert(const http::request<Body>& req, ServerContext& ctx) {
    try {
        auto body = nlohmann::json::parse(req.body());

        if (body.is_array()) {
            for (auto& entry : body) ctx.backlog.Add(std::move(entry));
        } else if (body.is_object()) {
            ctx.backlog.Add(std::move(body));
        } else {
            return MakeFailResp(400, "Body must be a JSON object or array", req,
                                ctx.config.allow_origin);
        }

        return MakeOKResp({{"status", "accepted"}}, req, ctx.config.allow_origin);
    } catch (const nlohmann::json::parse_error& e) {
        return MakeFailResp(400, std::format("Invalid JSON: {}", e.what()), req,
                            ctx.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_INSERT_HPP_
