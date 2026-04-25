#pragma once

#include "common.hpp"
#include "../globals.hpp"
#include "../log.hpp"
#include "../utils.hpp"

#include <stdexcept>
#include <unordered_set>

namespace loglite::handlers {

template <class Body>
http::response<http::string_body> handle_query(const http::request<Body>& req, ServerContext& ctx) {
    auto [path, qs] = split_target(req.target());
    auto params = parse_query_string(qs);

    // ── Validate required params ──────────────────────────────────────────────
    for (const auto* p : {"fields", "limit", "offset"}) {
        if (!params.contains(p))
            return fail(400, std::format("Required parameter '{}' is missing", p), req,
                        ctx.config.allow_origin);
    }

    // ── Extract pagination / field selection ──────────────────────────────────
    auto fields_str = params.find("fields")->second;
    auto limit = std::stoi(params.find("limit")->second);
    auto offset = std::stoi(params.find("offset")->second);

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
        auto key_filters = parse_filter_expr(key, value);
        if (key_filters.empty())
            return fail(400, std::format("Invalid filter expression for field '{}'", key), req,
                        ctx.config.allow_origin);
        for (auto& f : key_filters) filters.push_back(std::move(f));
    }

    if (ctx.config.debug)
        log::debug(std::format("Query fields={} limit={} offset={} filters={}", fields_str, limit,
                               offset, filters.size()));

    // ── Execute ───────────────────────────────────────────────────────────────
    try {
        Timer t;
        auto result = ctx.db.query(fields, filters, limit, offset);
        ctx.query_stats.collect(1, t.elapsed_ms());
        return ok(result.to_json(), req, ctx.config.allow_origin);
    } catch (const std::exception& e) {
        log::error(std::format("Query error: {}", e.what()));
        return fail(500, e.what(), req, ctx.config.allow_origin);
    }
}

}  // namespace loglite::handlers
