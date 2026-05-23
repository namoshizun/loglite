#ifndef BURN_HTTP_HPP_
#define BURN_HTTP_HPP_

#include "config.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace burn {

namespace detail {

inline void ParseHostPort(std::string_view authority, Endpoint& ep) {
    if (authority.starts_with('[')) {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            throw std::runtime_error("invalid IPv6 endpoint");
        }
        ep.host = std::string{authority.substr(0, close + 1)};
        authority.remove_prefix(close + 1);
        if (authority.empty()) {
            return;
        }
        if (authority[0] != ':') {
            throw std::runtime_error("invalid endpoint host/port");
        }
        ep.port = std::string{authority.substr(1)};
        return;
    }

    const auto colon = authority.rfind(':');
    if (colon == std::string_view::npos) {
        ep.host = std::string{authority};
        ep.port = "80";
        return;
    }
    ep.host = std::string{authority.substr(0, colon)};
    ep.port = std::string{authority.substr(colon + 1)};
    if (ep.host.empty() || ep.port.empty()) {
        throw std::runtime_error("invalid endpoint host/port");
    }
}

}  // namespace detail

[[nodiscard]] inline Endpoint ParseEndpoint(std::string_view url) {
    constexpr std::string_view kHttp = "http://";
    if (!url.starts_with(kHttp)) {
        throw std::runtime_error("endpoint must start with http://");
    }
    url.remove_prefix(kHttp.size());

    const auto slash = url.find('/');
    const std::string_view authority = slash == std::string_view::npos ? url : url.substr(0, slash);
    if (authority.empty()) {
        throw std::runtime_error("endpoint missing host");
    }

    Endpoint ep;
    detail::ParseHostPort(authority, ep);
    return ep;
}

[[nodiscard]] inline http::response<http::string_body> HttpGetSync(const Endpoint& ep,
                                                                   std::string_view target) {
    asio::io_context ioc;
    tcp::resolver resolver{ioc};
    beast::tcp_stream stream{ioc};
    stream.connect(resolver.resolve(ep.host, ep.port));

    http::request<http::string_body> req{http::verb::get, std::string(target), 11};
    req.set(http::field::host, ep.host);
    req.set(http::field::user_agent, "loglite-burn");
    req.keep_alive(false);

    http::write(stream, req);

    beast::flat_buffer buf;
    http::response<http::string_body> res;
    http::read(stream, buf, res);

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return res;
}

}  // namespace burn

#endif
