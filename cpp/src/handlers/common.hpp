#ifndef LOGLITE_HANDLERS_COMMON_HPP_
#define LOGLITE_HANDLERS_COMMON_HPP_

#include "../types.hpp"
#include "../utils.hpp"

#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace http = boost::beast::http;

namespace loglite::handlers {

// ── Response helpers ──────────────────────────────────────────────────────────

template <class Body>
inline http::response<http::string_body> MakeJSONResponse(http::status status,
                                                          const nlohmann::json& body,
                                                          const http::request<Body>& req,
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

template <class Body>
inline http::response<http::string_body> MakeOKResp(const nlohmann::json& body,
                                                    const http::request<Body>& req,
                                                    std::string_view origin = "*") {
    return MakeJSONResponse(http::status::ok, body, req, origin);
}

template <class Body>
inline http::response<http::string_body> MakeFailResp(int status_code, std::string_view msg,
                                                      const http::request<Body>& req,
                                                      std::string_view origin = "*") {
    return MakeJSONResponse(static_cast<http::status>(status_code), {{"error", msg}}, req, origin);
}

template <class Body>
inline http::response<http::string_body> MakeNotAvailableResp(const nlohmann::json& body,
                                                              const http::request<Body>& req,
                                                              std::string_view origin = "*") {
    return MakeJSONResponse(http::status::service_unavailable, body, req, origin);
}

// ── Query-string parsing ──────────────────────────────────────────────────────

// Parse a raw query string ("k1=v1&k2=v2") into a multimap.
// Values are URL-decoded.
inline std::unordered_multimap<std::string, std::string> ParseQueryString(std::string_view qs) {
    std::unordered_multimap<std::string, std::string> out;
    while (!qs.empty()) {
        auto amp = qs.find('&');
        auto pair = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);
        qs = (amp == std::string_view::npos) ? "" : qs.substr(amp + 1);

        auto eq = pair.find('=');
        if (eq == std::string_view::npos) continue;
        std::string key = url_decode(pair.substr(0, eq));
        std::string value = url_decode(pair.substr(eq + 1));
        out.emplace(std::move(key), std::move(value));
    }
    return out;
}

// Split the target into (path, query_string).
inline std::pair<std::string, std::string> SplitURLTarget(std::string_view target) {
    auto q = target.find('?');
    if (q == std::string_view::npos) return {std::string(target), ""};
    return {std::string(target.substr(0, q)), std::string(target.substr(q + 1))};
}

// Safe integer parsing for query parameters.  Returns std::nullopt on
// non-numeric or out-of-range input instead of throwing.
inline std::optional<int> ParseIntParam(std::string_view s) {
    if (s.empty()) return std::nullopt;

    // Reject inputs that contain anything other than digits and an optional leading '-'.
    for (char c : s) {
        if (!std::isdigit(c) && c != '-') {
            return std::nullopt;
        }
    }

    try {
        return std::stoi(std::string(s));
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
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
