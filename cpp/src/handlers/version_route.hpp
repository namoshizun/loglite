#ifndef LOGLITE_HANDLERS_VERSION_ROUTE_HPP_
#define LOGLITE_HANDLERS_VERSION_ROUTE_HPP_

#include "common.hpp"
#include "../globals.hpp"
#include "version.hpp"

namespace loglite::handlers {

template <class Body>
http::response<http::string_body> HandleVersion(const http::request<Body>& req,
                                                ServerContext& ctx) {
    return MakeOKResp({{"version", std::string{kVersion}}}, req, ctx.config.allow_origin);
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_VERSION_ROUTE_HPP_
