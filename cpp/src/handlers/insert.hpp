#ifndef LOGLITE_HANDLERS_INSERT_HPP_
#define LOGLITE_HANDLERS_INSERT_HPP_

#include "common.hpp"
#include "../access.hpp"
#include "../metrics.hpp"

#include <algorithm>

namespace loglite::handlers {

inline asio::awaitable<http::response<http::string_body>> HandleInsert(const Request& req,
                                                                       HttpAccess http) {
    metrics::MetricsRegistry::Instance().Collect(metrics::kIngestRequest,
                                                 static_cast<double>(req.body().size()));

    try {
        auto body = nlohmann::json::parse(req.body());
        std::vector<nlohmann::json> entries;
        if (body.is_array()) {
            if (!std::ranges::all_of(body, [](const auto& entry) { return entry.is_object(); }))
                co_return MakeFailResp(400, "Array entries must be JSON objects", req,
                                       http.config.allow_origin);
            entries.assign(body.begin(), body.end());
        } else if (body.is_object()) {
            entries.push_back(std::move(body));
        } else {
            co_return MakeFailResp(400, "Body must be a JSON object or array", req,
                                   http.config.allow_origin);
        }

        int admitted = 0;
        nlohmann::json rejected = nlohmann::json::array();
        for (size_t index = 0; index < entries.size(); ++index) {
            auto result = http.ingestion.Submit(std::move(entries[index]));
            if (result.admitted) {
                ++admitted;
            } else {
                rejected.push_back({{"index", index}, {"reason", result.reason}});
            }
        }

        if (admitted == 0 && !rejected.empty())
            co_return MakeFailResp(400, rejected.front()["reason"].get<std::string>(), req,
                                   http.config.allow_origin);

        co_return MakeOKResp(
            {{"status", "accepted"}, {"admitted", admitted}, {"rejected", rejected}}, req,
            http.config.allow_origin);
    } catch (const nlohmann::json::parse_error& e) {
        co_return MakeFailResp(400, fmt::format("Invalid JSON: {}", e.what()), req,
                               http.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_INSERT_HPP_
