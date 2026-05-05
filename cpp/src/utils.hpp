#ifndef LOGLITE_UTILS_HPP_
#define LOGLITE_UTILS_HPP_

#include <chrono>
#include <cstdint>
#include <format>
#include <limits>
#include <mutex>
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

// ── RAII timer ────────────────────────────────────────────────────────────────

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

// ── Stats tracker ─────────────────────────────────────────────────────────────

class StatsTracker {
   public:
    struct Snapshot {
        int64_t count{};
        double total_ms{};
        double avg_ms{};
        double max_ms{};
        double min_ms{};
    };

    void collect(int64_t n, double cost_ms) {
        std::lock_guard lk(mtx_);
        count_ += n;
        total_ms_ += cost_ms;
        max_ms_ = std::max(max_ms_, cost_ms);
        min_ms_ = std::min(min_ms_, cost_ms);
    }

    Snapshot get_and_reset() {
        std::lock_guard lk(mtx_);
        Snapshot s{
            count_,
            total_ms_,
            count_ > 0 ? total_ms_ / static_cast<double>(count_) : 0.0,
            max_ms_,
            min_ms_ == std::numeric_limits<double>::max() ? 0.0 : min_ms_,
        };
        count_ = 0;
        total_ms_ = 0;
        max_ms_ = 0;
        min_ms_ = std::numeric_limits<double>::max();
        return s;
    }

   private:
    std::mutex mtx_;
    int64_t count_{};
    double total_ms_{};
    double max_ms_{};
    double min_ms_{std::numeric_limits<double>::max()};
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
