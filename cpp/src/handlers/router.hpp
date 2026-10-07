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
#include "../access.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <array>
#include <optional>
#include <string_view>

namespace http = boost::beast::http;
namespace asio = boost::asio;

namespace loglite::handlers {

using StringResponse = http::response<http::string_body>;
using RouteHandler = asio::awaitable<StringResponse> (*)(const Request&, HttpAccess);

struct RouteEntry {
    std::string_view path;
    http::verb method;
    RouteHandler handler;
};

constexpr std::array kRoutes{
    RouteEntry{"/logs", http::verb::post, &HandleInsert},
    RouteEntry{"/logs", http::verb::get, &HandleQuery},
    RouteEntry{"/health", http::verb::get, &HandleHealth},
    RouteEntry{"/version", http::verb::get, &HandleVersion},
    RouteEntry{"/stats", http::verb::get, &HandleStats},
    RouteEntry{"/settings", http::verb::get, &HandleSettings},
    RouteEntry{"/schema", http::verb::get, &HandleSchema},
};

inline asio::awaitable<std::optional<StringResponse>> Dispatch(const Request& req,
                                                               HttpAccess http) {
    for (const auto& route : kRoutes) {
        if (req.path() == route.path && req.method() == route.method) {
            co_return co_await route.handler(req, http);
        }
    }
    co_return std::nullopt;
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_ROUTER_HPP_
