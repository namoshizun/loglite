#ifndef LOGLITE_HANDLERS_ROUTER_HPP_
#define LOGLITE_HANDLERS_ROUTER_HPP_

#include "common.hpp"
#include "health.hpp"
#include "insert.hpp"
#include "query.hpp"
#include "schema.hpp"
#include "settings.hpp"
#include "stats.hpp"
#include "version_route.hpp"
#include "../context.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <array>
#include <optional>
#include <string_view>

namespace http = boost::beast::http;
namespace asio = boost::asio;

namespace loglite::handlers {

using StringRequest = http::request<http::string_body>;
using StringResponse = http::response<http::string_body>;
using RouteHandler = asio::awaitable<StringResponse> (*)(const StringRequest&, ServerContext&);

struct RouteEntry {
    std::string_view path;
    http::verb method;
    RouteHandler handler;
};

constexpr std::array kRoutes{
    RouteEntry{"/logs", http::verb::post, &HandleInsert<http::string_body>},
    RouteEntry{"/logs", http::verb::get, &HandleQuery<http::string_body>},
    RouteEntry{"/health", http::verb::get, &HandleHealth<http::string_body>},
    RouteEntry{"/version", http::verb::get, &HandleVersion<http::string_body>},
    RouteEntry{"/stats", http::verb::get, &HandleStats<http::string_body>},
    RouteEntry{"/settings", http::verb::get, &HandleSettings<http::string_body>},
    RouteEntry{"/schema", http::verb::get, &HandleSchema<http::string_body>},
};

inline asio::awaitable<std::optional<StringResponse>> Dispatch(std::string_view path,
                                                               http::verb method,
                                                               const StringRequest& req,
                                                               ServerContext& ctx) {
    for (const auto& route : kRoutes) {
        if (path == route.path && method == route.method) {
            co_return co_await route.handler(req, ctx);
        }
    }
    co_return std::nullopt;
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_ROUTER_HPP_
