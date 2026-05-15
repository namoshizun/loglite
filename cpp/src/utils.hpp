#ifndef LOGLITE_UTILS_HPP_
#define LOGLITE_UTILS_HPP_

#include <date/date.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace loglite {

// C++20 equivalent of std::ranges::contains (C++23). Uses iterator-pair find so
// temporaries (non-borrowed ranges) do not collapse to std::ranges::dangling.
template <std::ranges::input_range R, class T>
constexpr bool range_contains(R&& r, const T& value) {
    const auto first = std::ranges::begin(r);
    const auto last = std::ranges::end(r);
    return std::ranges::find(first, last, value) != last;
}

// ── Size conversions ──────────────────────────────────────────────────────────

inline int64_t parse_size_to_bytes(std::string_view s) {
    // Accepts: "500MB", "1TB", "800GB", "2KB", "4GB"
    static constexpr std::string_view units[] = {"B", "KB", "MB", "GB", "TB"};
    static constexpr int64_t mults[] = {
        1LL, 1024LL, 1024LL * 1024, 1024LL * 1024 * 1024, 1024LL * 1024 * 1024 * 1024,
    };

    for (int i = 4; i >= 0; --i) {
        auto unit = units[i];
        if (s.ends_with(unit)) {
            auto num_part = s.substr(0, s.size() - unit.size());
            int64_t n = std::stoll(std::string(num_part));
            return n * mults[i];
        }
    }
    throw std::invalid_argument(std::format("Invalid size string: '{}'", s));
}

inline double bytes_to_mb(int64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

// ── Timer ────────────────────────────────────────────────────────────────

class Timer {
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_{Clock::now()};

   public:
    Timer() = default;

    double elapsed_ms() const {
        using std::chrono_literals::operator""ms;
        return (Clock::now() - start_) / 1.0ms;
    }

    double elapsed_s() const {
        using std::chrono_literals::operator""s;
        return (Clock::now() - start_) / 1.0s;
    }
};

// ── URL helpers ───────────────────────────────────────────────────────────────

inline std::string url_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = std::string(s.substr(i + 1, 2));
            // Safely parse hex; skip the escape sequence on invalid input.
            try {
                out += static_cast<char>(std::stoi(hex, nullptr, 16));
            } catch (const std::invalid_argument&) {
            } catch (const std::out_of_range&) {
            }
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

// Trims leading and trailing characters for which std::isspace is true (calls use unsigned char).
inline std::string_view strip_spaces(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// ── Time utils ───────────────────────────────────────────────────────────────
inline std::string format_utc(std::chrono::system_clock::time_point tp) {
    auto secs = std::chrono::floor<std::chrono::seconds>(tp.time_since_epoch());
    return date::format("%Y-%m-%dT%H:%M:%SZ", date::sys_seconds{secs});
}

// RFC 3339 / ISO 8601 instant: full date-time with 'T', optional fractional seconds, and optional
// offset ('Z', ±HH:MM via %Ez, or ±HHMM via %z). No time zone ⇒ fields are interpreted as UTC
// (same as a plain sys_time parse). Date-only inputs are rejected.
inline std::optional<std::chrono::system_clock::time_point> parse_iso8601(std::string_view s) {
    using std::chrono::system_clock;

    std::string_view t = strip_spaces(s);
    if (t.find('T') == std::string_view::npos) return std::nullopt;

    const std::string buf{t};
    date::sys_time<std::chrono::nanoseconds> parsed{};

    constexpr const char* fmts[] = {
        "%FT%TZ",
        "%FT%T%Ez",
        "%FT%T%z",
        "%FT%T",
    };
    for (const char* fmt : fmts) {
        std::istringstream is(buf);
        parsed = {};
        // NOTE: Use date::from_stream directly: libstdc++ (GCC 14+) also provides
        // std::chrono::from_stream for sys_time, so date::parse()'s unqualified
        // from_stream(...) is ambiguous on Linux.
        date::from_stream(is, fmt, parsed);
        if (is.fail()) continue;

        while (is.peek() != std::istringstream::traits_type::eof() &&
               std::isspace(static_cast<unsigned char>(is.peek()))) {
            static_cast<void>(is.get());
        }
        if (is.peek() != std::istringstream::traits_type::eof()) continue;

        return system_clock::time_point(
            std::chrono::duration_cast<system_clock::duration>(parsed.time_since_epoch()));
    }
    return std::nullopt;
}

}  // namespace loglite

#endif  // LOGLITE_UTILS_HPP_
