#ifndef LOGLITE_HANDLERS_REQUEST_HPP_
#define LOGLITE_HANDLERS_REQUEST_HPP_

#include "../utils.hpp"

#include <boost/beast/http.hpp>

#include <cctype>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace http = boost::beast::http;

namespace loglite::handlers {

// ── Query-string parsing ──────────────────────────────────────────────────────

using QueryParams = std::unordered_multimap<std::string, std::string>;

// Parse a raw query string ("k1=v1&k2=v2") into a multimap.
// Values are URL-decoded.
inline QueryParams ParseQueryString(std::string_view qs) {
    QueryParams out;
    while (!qs.empty()) {
        auto amp = qs.find('&');
        auto pair = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);
        qs = (amp == std::string_view::npos) ? "" : qs.substr(amp + 1);

        auto eq = pair.find('=');
        if (eq == std::string_view::npos) continue;

        std::string key = url_decode(pair.substr(0, eq));
        std::string value = url_decode(pair.substr(eq + 1));
        out.emplace(std::move(key), std::move(value));
    }

    return out;
}

// Split the target into (path, query_string).
inline std::pair<std::string, std::string> SplitURLTarget(std::string_view target) {
    auto q = target.find('?');
    if (q == std::string_view::npos) return {std::string(target), ""};
    return {std::string(target.substr(0, q)), std::string(target.substr(q + 1))};
}

// Safe integer parsing for query parameters.  Returns std::nullopt on
// non-numeric or out-of-range input instead of throwing.
inline std::optional<int> ParseIntParam(std::string_view s) {
    if (s.empty()) return std::nullopt;

    // Reject inputs that contain anything other than digits and an optional leading '-'.
    for (char c : s) {
        if (!std::isdigit(c) && c != '-') {
            return std::nullopt;
        }
    }

    try {
        return std::stoi(std::string(s));
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

// ── Request ───────────────────────────────────────────────────────────────────

// An inbound HTTP request with its target parsed once up front. Handlers read the request
// through this type instead of touching the raw Beast message.
class Request {
   public:
    explicit Request(http::request<http::string_body> raw) : raw_(std::move(raw)) {
        auto [path, query] = SplitURLTarget(raw_.target());
        path_ = std::move(path);
        params_ = ParseQueryString(query);
    }

    http::verb method() const { return raw_.method(); }
    const std::string& path() const { return path_; }
    const std::string& body() const { return raw_.body(); }
    unsigned version() const { return raw_.version(); }
    bool keep_alive() const { return raw_.keep_alive(); }

    // All query parameters; repeated keys are preserved.
    const QueryParams& params() const { return params_; }
    bool HasParam(std::string_view name) const { return params_.contains(std::string(name)); }

    // First value of `name`; the view is valid as long as this Request is.
    std::optional<std::string_view> Param(std::string_view name) const {
        const auto it = params_.find(std::string(name));
        if (it == params_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<int> IntParam(std::string_view name) const {
        const auto value = Param(name);
        return value ? ParseIntParam(*value) : std::nullopt;
    }

    // Comma-separated list parameter ("a,b,c"). Items are kept verbatim, empties included, so that
    // malformed lists fail field validation downstream instead of being silently repaired.
    // Pass `strip_items` for endpoints that tolerate padding ("a, b").
    std::optional<std::vector<std::string>> ListParam(std::string_view name,
                                                      bool strip_items = false) const {
        const auto value = Param(name);
        if (!value) return std::nullopt;
        // views::split yields zero pieces for an empty string, but "" is one (invalid) item.
        if (value->empty()) return std::vector<std::string>{""};

        std::vector<std::string> items;
        for (auto part : std::views::split(*value, ',')) {
            const std::string_view item(part.begin(), part.end());
            items.emplace_back(strip_items ? strip_spaces(item) : item);
        }
        return items;
    }

   private:
    http::request<http::string_body> raw_;
    std::string path_;
    QueryParams params_;
};

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_REQUEST_HPP_
