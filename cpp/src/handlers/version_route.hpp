#ifndef LOGLITE_HANDLERS_VERSION_ROUTE_HPP_
#define LOGLITE_HANDLERS_VERSION_ROUTE_HPP_

#include "common.hpp"
#include "../context.hpp"
#include "version.hpp"

#include <boost/asio.hpp>

namespace asio = boost::asio;

namespace loglite::handlers {

template <class Body>
asio::awaitable<http::response<http::string_body>> HandleVersion(const http::request<Body>& req,
                                                                 ServerContext& ctx) {
    co_return MakeOKResp({{"version", std::string{kVersion}}}, req, ctx.config.allow_origin);
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_VERSION_ROUTE_HPP_
