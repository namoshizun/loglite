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

inline Endpoint ParseEndpoint(const std::string& url) {
    constexpr std::string_view kHttp = "http://";
    if (!url.starts_with(kHttp)) {
        throw std::runtime_error("endpoint must start with http://");
    }
    std::string rest{url.substr(kHttp.size())};
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        rest = rest.substr(0, slash);
    }
    if (rest.empty()) {
        throw std::runtime_error("endpoint missing host");
    }

    Endpoint ep;
    if (rest.starts_with('[')) {
        auto close = rest.find(']');
        if (close == std::string::npos) {
            throw std::runtime_error("invalid IPv6 endpoint");
        }
        ep.host = rest.substr(0, close + 1);
        rest = rest.substr(close + 1);
        if (rest.empty()) {
            return ep;
        }
        if (rest[0] != ':') {
            throw std::runtime_error("invalid endpoint host/port");
        }
        ep.port = rest.substr(1);
        return ep;
    }

    auto colon = rest.rfind(':');
    if (colon == std::string::npos) {
        ep.host = std::move(rest);
        ep.port = "80";
        return ep;
    }
    ep.host = rest.substr(0, colon);
    ep.port = rest.substr(colon + 1);
    if (ep.host.empty() || ep.port.empty()) {
        throw std::runtime_error("invalid endpoint host/port");
    }
    return ep;
}

inline http::response<http::string_body> HttpGetSync(const Endpoint& ep, std::string_view target) {
    asio::io_context ioc;
    tcp::resolver resolver{ioc};
    beast::tcp_stream stream{ioc};
    auto results = resolver.resolve(ep.host, ep.port);
    stream.connect(results);

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
