#ifndef LOGLITE_UTILS_HPP_
#define LOGLITE_UTILS_HPP_

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <ranges>
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
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }

    double elapsed_s() const { return elapsed_ms() / 1000.0; }
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

}  // namespace loglite

#endif  // LOGLITE_UTILS_HPP_
