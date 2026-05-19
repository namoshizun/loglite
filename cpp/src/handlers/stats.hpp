#ifndef LOGLITE_HANDLERS_STATS_HPP_
#define LOGLITE_HANDLERS_STATS_HPP_

#include "common.hpp"
#include "../globals.hpp"
#include "../log.hpp"

#include <chrono>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>

namespace loglite::handlers {

using namespace std::chrono_literals;

template <class Body>
http::response<http::string_body> HandleStats(const http::request<Body>& req, ServerContext& ctx) {
    auto [path, qs] = SplitURLTarget(req.target());
    auto params = ParseQueryString(qs);

    // ── Validate required params ──────────────────────────────────────────────
    for (const auto* p : {"since", "until", "activity_stats_fields", "database_stats_fields"}) {
        if (!params.contains(p))
            return MakeFailResp(400, std::format("Required parameter '{}' is missing", p), req,
                                ctx.config.allow_origin);
    }

    auto since_str = params.find("since")->second;
    auto until_str = params.find("until")->second;
    auto activity_fields_str = params.find("activity_stats_fields")->second;
    auto database_fields_str = params.find("database_stats_fields")->second;

    std::string ordering = "desc";
    if (auto it = params.find("ordering"); it != params.end()) {
        ordering = it->second;
        if (ordering != "asc" && ordering != "desc")
            return MakeFailResp(400, "Parameter 'ordering' must be 'asc' or 'desc'", req,
                                ctx.config.allow_origin);
    }

    // ── Parse timestamps, validate window ≤ 1 day ────────────────────────────
    auto since_tp = loglite::parse_iso8601(since_str);
    auto until_tp = loglite::parse_iso8601(until_str);
    if (!since_tp || !until_tp)
        return MakeFailResp(400, "'since' and 'until' must be ISO-8601 timestamps", req,
                            ctx.config.allow_origin);

    if (*until_tp <= *since_tp)
        return MakeFailResp(400, "'until' must be after 'since'", req, ctx.config.allow_origin);

    if (*until_tp - *since_tp > 24h)
        return MakeFailResp(400, "Time window must not exceed 1 day", req, ctx.config.allow_origin);

    // ── Resolve field lists ──────────────────────────────────────────────────
    auto split_fields = [](std::string_view s) -> std::vector<std::string> {
        if (s == "*") return {"*"};
        std::vector<std::string> out;
        for (auto part : std::views::split(s, ',')) {
            std::string_view raw(std::ranges::begin(part), std::ranges::end(part));
            auto piece = loglite::strip_spaces(raw);
            if (!piece.empty()) out.emplace_back(piece);
        }
        return out;
    };

    // ── Execute queries ───────────────────────────────────────────────────────
    try {
        auto activities = ctx.db_read.UseConnection([&](ReaderDatabase& r) {
            return r.QueryActivityStats(since_str, until_str, split_fields(activity_fields_str),
                                        ordering);
        });
        auto database = ctx.db_read.UseConnection([&](ReaderDatabase& r) {
            return r.QueryDatabaseStats(since_str, until_str, split_fields(database_fields_str),
                                        ordering);
        });

        nlohmann::json body{
            {"activities",
             {{"fields", std::move(activities.fields)}, {"data", std::move(activities.data)}}},
            {"database",
             {{"fields", std::move(database.fields)}, {"data", std::move(database.data)}}},
        };
        return MakeOKResp(body, req, ctx.config.allow_origin);
    } catch (const std::exception& e) {
        log::error(std::format("Stats query error: {}", e.what()));
        return MakeFailResp(500, e.what(), req, ctx.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_STATS_HPP_
