#ifndef LOGLITE_HANDLERS_QUERY_HPP_
#define LOGLITE_HANDLERS_QUERY_HPP_

#include "common.hpp"
#include "../globals.hpp"
#include "../log.hpp"
#include "../metrics.hpp"
#include "../utils.hpp"

#include <stdexcept>
#include <unordered_set>

namespace loglite::handlers {

template <class Body>
http::response<http::string_body> HandleQuery(const http::request<Body>& req, ServerContext& ctx) {
    metrics::ObservationTimer request_timer{metrics::kQueryRequest};

    auto [path, qs] = SplitURLTarget(req.target());
    auto params = ParseQueryString(qs);

    // ── Validate required params ──────────────────────────────────────────────
    for (const auto* p : {"fields", "limit", "offset"}) {
        if (!params.contains(p))
            return MakeFailResp(400, std::format("Required parameter '{}' is missing", p), req,
                                ctx.config.allow_origin);
    }

    // ── Extract pagination / field selection ──────────────────────────────────
    auto fields_str = params.find("fields")->second;
    auto limit_opt = ParseIntParam(params.find("limit")->second);
    auto offset_opt = ParseIntParam(params.find("offset")->second);
    if (!limit_opt || !offset_opt)
        return MakeFailResp(400, "Parameters 'limit' and 'offset' must be integers", req,
                            ctx.config.allow_origin);
    auto limit = *limit_opt;
    auto offset = *offset_opt;

    std::vector<std::string> fields;
    if (fields_str == "*") {
        fields = {"*"};
    } else {
        for (auto sv : std::views::split(fields_str, ',')) {
            fields.emplace_back(sv.begin(), sv.end());
        }
    }

    // ── Build filters from remaining params ───────────────────────────────────
    static const std::unordered_set<std::string> reserved{"fields", "limit", "offset"};
    std::vector<QueryFilter> filters;

    for (const auto& [key, value] : params) {
        if (reserved.contains(key)) continue;
        auto key_filters = ParseQueryFilters(key, value);
        if (key_filters.empty())
            return MakeFailResp(400, std::format("Invalid filter expression for field '{}'", key),
                                req, ctx.config.allow_origin);
        for (auto& f : key_filters) filters.push_back(std::move(f));
    }

    if (ctx.config.debug)
        log::debug(std::format("Query fields={} limit={} offset={} filters={}", fields_str, limit,
                               offset, filters.size()));

    // ── Execute ───────────────────────────────────────────────────────────────
    try {
        auto result = ctx.db.Query(fields, filters, limit, offset);
        return MakeOKResp(result.to_json(), req, ctx.config.allow_origin);
    } catch (const std::exception& e) {
        log::error(std::format("Query error: {}", e.what()));
        return MakeFailResp(500, e.what(), req, ctx.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_QUERY_HPP_
