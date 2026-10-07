#ifndef LOGLITE_HANDLERS_HEALTH_HPP_
#define LOGLITE_HANDLERS_HEALTH_HPP_

#include "common.hpp"
#include "../access.hpp"

namespace loglite::handlers {

inline asio::awaitable<http::response<http::string_body>> HandleHealth(const Request& req,
                                                                       HttpAccess http) {
    bool ok_flag = co_await http.queries.Ping();
    if (ok_flag) {
        co_return MakeOKResp({{"status", "ok"}}, req, http.config.allow_origin);
    } else {
        co_return MakeNotAvailableResp({{"status", "error"}}, req, http.config.allow_origin);
    }
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_HEALTH_HPP_
