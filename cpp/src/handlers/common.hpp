#ifndef LOGLITE_HANDLERS_COMMON_HPP_
#define LOGLITE_HANDLERS_COMMON_HPP_

#include "request.hpp"
#include "../types.hpp"
#include "../utils.hpp"

#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <regex>
#include <string>
#include <vector>

namespace http = boost::beast::http;

namespace loglite::handlers {

// ── Response helpers ──────────────────────────────────────────────────────────

inline http::response<http::string_body> MakeJSONResponse(http::status status,
                                                          const nlohmann::json& body,
                                                          const Request& req,
                                                          std::string_view allow_origin = "*") {
    http::response<http::string_body> res{status, req.version()};
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, allow_origin);
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.keep_alive(req.keep_alive());
    res.body() = body.dump();
    res.prepare_payload();
    return res;
}

inline http::response<http::string_body> MakeOKResp(const nlohmann::json& body, const Request& req,
                                                    std::string_view origin = "*") {
    return MakeJSONResponse(http::status::ok, body, req, origin);
}

inline http::response<http::string_body> MakeFailResp(int status_code, std::string_view msg,
                                                      const Request& req,
                                                      std::string_view origin = "*") {
    return MakeJSONResponse(static_cast<http::status>(status_code), {{"error", msg}}, req, origin);
}

inline http::response<http::string_body> MakeNotAvailableResp(const nlohmann::json& body,
                                                              const Request& req,
                                                              std::string_view origin = "*") {
    return MakeJSONResponse(http::status::service_unavailable, body, req, origin);
}

// ── Filter expression parser ──────────────────────────────────────────────────
//
// Each query param value is one or more "<op><value>" tokens, comma-separated.
// e.g. ">=2024-01-01T00:00:00,<=2024-01-02T00:00:00"
inline std::vector<QueryFilter> ParseQueryFilters(std::string_view field, std::string_view expr) {
    static const std::regex re{R"((>=|<=|!=|~=|=|>|<)([^,]+))"};

    std::vector<QueryFilter> filters;
    auto begin = std::cregex_iterator(expr.data(), expr.data() + expr.size(), re);
    auto end = std::cregex_iterator{};

    for (auto it = begin; it != end; ++it) {
        std::string op = (*it)[1].str();
        std::string val = (*it)[2].str();

        // Trim trailing whitespace that might appear after url-decode.
        while (!val.empty() && val.back() == ' ') val.pop_back();

        filters.push_back({std::string(field), std::move(op), std::move(val)});
    }

    return filters;
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_COMMON_HPP_
