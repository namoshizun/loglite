#ifndef LOGLITE_HANDLERS_VERSION_ROUTE_HPP_
#define LOGLITE_HANDLERS_VERSION_ROUTE_HPP_

#include "common.hpp"
#include "../access.hpp"
#include "version.hpp"

#include <boost/asio.hpp>

namespace asio = boost::asio;

namespace loglite::handlers {

inline asio::awaitable<http::response<http::string_body>> HandleVersion(const Request& req,
                                                                        HttpAccess http) {
    co_return MakeOKResp({{"version", std::string{kVersion}}}, req, http.config.allow_origin);
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_VERSION_ROUTE_HPP_
