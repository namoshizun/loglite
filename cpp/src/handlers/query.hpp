#ifndef LOGLITE_HANDLERS_QUERY_HPP_
#define LOGLITE_HANDLERS_QUERY_HPP_

#include "common.hpp"
#include "../context.hpp"
#include "../log.hpp"
#include "../metrics.hpp"
#include "../utils.hpp"

#include <fmt/ranges.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace loglite::handlers {

inline asio::awaitable<http::response<http::string_body>> HandleQuery(const Request& req,
                                                                      ServerContext& ctx) {
    metrics::ObservationTimer request_timer{metrics::kQueryRequest};

    // ── Validate required params ──────────────────────────────────────────────
    for (const auto* p : {"fields", "limit", "offset"}) {
        if (!req.HasParam(p))
            co_return MakeFailResp(400, fmt::format("Required parameter '{}' is missing", p), req,
                                   ctx.config.allow_origin);
    }

    // ── Extract pagination / field selection ──────────────────────────────────
    const auto limit_opt = req.IntParam("limit");
    const auto offset_opt = req.IntParam("offset");

    if (!limit_opt || !offset_opt)
        co_return MakeFailResp(400, "Parameters 'limit' and 'offset' must be integers", req,
                               ctx.config.allow_origin);

    auto limit = *limit_opt;
    auto offset = *offset_opt;

    if (limit < 1)
        co_return MakeFailResp(400, "'limit' must be a positive integer", req,
                               ctx.config.allow_origin);
    if (limit > kMaxQueryLimit)
        co_return MakeFailResp(400, fmt::format("'limit' must not exceed {}", kMaxQueryLimit), req,
                               ctx.config.allow_origin);
    if (offset < 0)
        co_return MakeFailResp(400, "'offset' must be a non-negative integer", req,
                               ctx.config.allow_origin);

    const auto fields = *req.ListParam("fields");

    // ── Build filters from remaining params ───────────────────────────────────
    static const std::unordered_set<std::string> reserved{"fields", "limit", "offset"};
    std::vector<QueryFilter> filters;

    for (const auto& [key, value] : req.params()) {
        if (reserved.contains(key)) continue;
        auto key_filters = ParseQueryFilters(key, value);
        if (key_filters.empty())
            co_return MakeFailResp(400,
                                   fmt::format("Invalid filter expression for field '{}'", key),
                                   req, ctx.config.allow_origin);
        std::ranges::move(key_filters, std::back_inserter(filters));
    }

    if (ctx.config.debug)
        log::DEBUG("Query fields={} limit={} offset={} filters={}", fmt::join(fields, ","), limit,
                   offset, filters.size());

    // ── Execute ───────────────────────────────────────────────────────────────
    try {
        auto result = co_await ctx.db_read.AsyncUseConnection(
            ctx.reader_executor,
            [&](LogReader& r) { return r.Query(fields, filters, limit, offset); });
        co_return MakeOKResp(result.ToJSON(), req, ctx.config.allow_origin);
    } catch (const std::exception& e) {
        log::ERROR("Query error: {}", e.what());
        co_return MakeFailResp(500, e.what(), req, ctx.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_QUERY_HPP_
