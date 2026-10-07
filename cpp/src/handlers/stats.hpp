#ifndef LOGLITE_HANDLERS_STATS_HPP_
#define LOGLITE_HANDLERS_STATS_HPP_

#include "common.hpp"
#include "../context.hpp"
#include "../log.hpp"

#include <chrono>
#include <fmt/format.h>
#include <stdexcept>
#include <string>

namespace loglite::handlers {

using namespace std::chrono_literals;

inline asio::awaitable<http::response<http::string_body>> HandleStats(const Request& req,
                                                                      ServerContext& ctx) {
    // ── Validate required params ──────────────────────────────────────────────
    for (const auto* p : {"since", "until", "activity_stats_fields", "database_stats_fields"}) {
        if (!req.HasParam(p))
            co_return MakeFailResp(400, fmt::format("Required parameter '{}' is missing", p), req,
                                   ctx.config.allow_origin);
    }

    const auto since_str = *req.Param("since");
    const auto until_str = *req.Param("until");
    const auto ordering = req.Param("ordering").value_or("desc");
    if (ordering != "asc" && ordering != "desc")
        co_return MakeFailResp(400, "Parameter 'ordering' must be 'asc' or 'desc'", req,
                               ctx.config.allow_origin);

    // ── Parse timestamps, validate window ≤ 1 day ────────────────────────────
    auto since_tp = loglite::parse_iso8601(since_str);
    auto until_tp = loglite::parse_iso8601(until_str);

    if (!since_tp || !until_tp)
        co_return MakeFailResp(400, "'since' and 'until' must be ISO-8601 timestamps", req,
                               ctx.config.allow_origin);

    if (*until_tp <= *since_tp)
        co_return MakeFailResp(400, "'until' must be after 'since'", req, ctx.config.allow_origin);

    if (*until_tp - *since_tp > 24h)
        co_return MakeFailResp(400, "Time window must not exceed 1 day", req,
                               ctx.config.allow_origin);

    // ── Execute queries ───────────────────────────────────────────────────────
    try {
        auto activities =
            co_await ctx.db_read.AsyncUseConnection(ctx.reader_executor, [&](LogReader& r) {
                return r.QueryActivityStats(
                    since_str, until_str,
                    *req.ListParam("activity_stats_fields", /*strip_items=*/true), ordering);
            });
        auto database =
            co_await ctx.db_read.AsyncUseConnection(ctx.reader_executor, [&](LogReader& r) {
                return r.QueryDatabaseStats(
                    since_str, until_str,
                    *req.ListParam("database_stats_fields", /*strip_items=*/true), ordering);
            });

        const auto uptime_s = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::steady_clock::now() - ctx.server_started_at)
                                  .count();

        nlohmann::json body{
            {"activities",
             {{"fields", std::move(activities.fields)}, {"data", std::move(activities.data)}}},
            {"database",
             {{"fields", std::move(database.fields)}, {"data", std::move(database.data)}}},
            {"uptime", uptime_s},
        };
        co_return MakeOKResp(body, req, ctx.config.allow_origin);
    } catch (const std::exception& e) {
        log::ERROR("Stats query error: {}", e.what());
        co_return MakeFailResp(500, e.what(), req, ctx.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_STATS_HPP_
