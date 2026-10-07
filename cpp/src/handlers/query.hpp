#ifndef LOGLITE_HANDLERS_QUERY_HPP_
#define LOGLITE_HANDLERS_QUERY_HPP_

#include "common.hpp"
#include "../access.hpp"
#include "../log.hpp"
#include "../metrics.hpp"
#include "../query_plan.hpp"
#include "../utils.hpp"

#include <fmt/ranges.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace loglite::handlers {

inline asio::awaitable<http::response<http::string_body>> HandleQuery(const Request& req,
                                                                      HttpAccess http) {
    metrics::ObservationTimer request_timer{metrics::kQueryRequest};

    // ── Validate required params ──────────────────────────────────────────────
    for (const auto* p : {"fields", "limit", "offset"}) {
        if (!req.HasParam(p))
            co_return MakeFailResp(400, fmt::format("Required parameter '{}' is missing", p), req,
                                   http.config.allow_origin);
    }

    // ── Extract pagination / field selection ──────────────────────────────────
    const auto limit_opt = req.IntParam("limit");
    const auto offset_opt = req.IntParam("offset");

    if (!limit_opt || !offset_opt)
        co_return MakeFailResp(400, "Parameters 'limit' and 'offset' must be integers", req,
                               http.config.allow_origin);

    auto limit = *limit_opt;
    auto offset = *offset_opt;

    if (limit < 1)
        co_return MakeFailResp(400, "'limit' must be a positive integer", req,
                               http.config.allow_origin);
    if (limit > kMaxQueryLimit)
        co_return MakeFailResp(400, fmt::format("'limit' must not exceed {}", kMaxQueryLimit), req,
                               http.config.allow_origin);
    if (offset < 0)
        co_return MakeFailResp(400, "'offset' must be a non-negative integer", req,
                               http.config.allow_origin);

    const auto fields = *req.ListParam("fields");

    // ── Build filters from remaining params ───────────────────────────────────
    static const std::unordered_set<std::string> reserved{"fields", "limit", "offset"};
    QueryRequest query{fields, {}, limit, offset};

    for (const auto& [key, value] : req.params()) {
        if (reserved.contains(key)) continue;
        auto key_filters = ParseQueryFilters(key, value);
        if (key_filters.empty())
            co_return MakeFailResp(400,
                                   fmt::format("Invalid filter expression for field '{}'", key),
                                   req, http.config.allow_origin);
        for (auto& filter : key_filters) {
            const auto op = ParseCmpOp(filter.op);
            if (!op)
                co_return MakeFailResp(400, fmt::format("Unknown query operator: '{}'", filter.op),
                                       req, http.config.allow_origin);
            query.predicates.push_back({std::move(filter.field), *op, std::move(filter.value)});
        }
    }

    if (http.config.debug)
        log::DEBUG("Query fields={} limit={} offset={} filters={}", fmt::join(fields, ","), limit,
                   offset, query.predicates.size());

    try {
        auto result = co_await http.queries.Execute(std::move(query));
        co_return MakeOKResp(result.ToJSON(), req, http.config.allow_origin);
    } catch (const std::exception& e) {
        log::ERROR("Query error: {}", e.what());
        co_return MakeFailResp(500, e.what(), req, http.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_QUERY_HPP_
