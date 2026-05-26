#ifndef LOGLITE_LOG_HPP_
#define LOGLITE_LOG_HPP_

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <utility>
#include <unistd.h>

namespace loglite::log {

enum class Level { kDebug, kInfo, kWarn, kError };

inline std::atomic<Level> g_min_level{Level::kInfo};

inline void SetLevel(Level level) { g_min_level.store(level, std::memory_order_relaxed); }

namespace detail {
inline bool enabled(Level level) {
    return static_cast<int>(level) >= static_cast<int>(g_min_level.load(std::memory_order_relaxed));
}
inline std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto s = std::chrono::floor<std::chrono::seconds>(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s);
    return fmt::format("{:%Y-%m-%dT%H:%M:%S}.{:03}", s, ms.count());
}

inline fmt::text_style style_for(Level level) {
    using fmt::color;
    using fmt::emphasis;
    using fmt::fg;
    switch (level) {
    case Level::kDebug:
        return fg(color::light_gray);
    case Level::kInfo:
        return fg(color::cyan);
    case Level::kWarn:
        return fg(color::yellow) | emphasis::bold;
    case Level::kError:
        return fg(color::red) | emphasis::bold;
    }
    return {};
}

inline std::string_view level_label(Level level) {
    switch (level) {
    case Level::kDebug:
        return "DEBUG";
    case Level::kInfo:
        return "INFO ";
    case Level::kWarn:
        return "WARN ";
    case Level::kError:
        return "ERROR";
    }
    return "?";
}

inline bool stream_supports_color(FILE* stream) {
    if (stream == nullptr) return false;

    static const bool kStdoutColor = isatty(fileno(stdout)) != 0;
    static const bool kStderrColor = isatty(fileno(stderr)) != 0;
    if (stream == stdout) return kStdoutColor;
    if (stream == stderr) return kStderrColor;

    return isatty(fileno(stream)) != 0;
}

template <typename... Args>
inline void log_formatted(FILE* stream, Level level, fmt::format_string<Args...> fmt,
                          Args&&... args) {
    if (!enabled(level)) return;
    const auto msg = fmt::format(fmt, std::forward<Args>(args)...);
    const auto ts = timestamp();
    const auto label = level_label(level);
    std::string line;
    if (stream_supports_color(stream)) {
        const auto levelStyled = fmt::format(style_for(level), "{}", label);
        line = fmt::format("[{}] [{}] {}\n", ts, levelStyled, msg);
    } else {
        line = fmt::format("[{}] [{}] {}\n", ts, label, msg);
    }
    std::fwrite(line.data(), 1, line.size(), stream);
}
}  // namespace detail

inline void INFO(std::string_view msg) { detail::log_formatted(stdout, Level::kInfo, "{}", msg); }

template <typename... Args>
inline void INFO(fmt::format_string<Args...> fmt, Args&&... args) {
    detail::log_formatted(stdout, Level::kInfo, fmt, std::forward<Args>(args)...);
}

inline void WARN(std::string_view msg) { detail::log_formatted(stderr, Level::kWarn, "{}", msg); }

template <typename... Args>
inline void WARN(fmt::format_string<Args...> fmt, Args&&... args) {
    detail::log_formatted(stderr, Level::kWarn, fmt, std::forward<Args>(args)...);
}

inline void ERROR(std::string_view msg) { detail::log_formatted(stderr, Level::kError, "{}", msg); }

template <typename... Args>
inline void ERROR(fmt::format_string<Args...> fmt, Args&&... args) {
    detail::log_formatted(stderr, Level::kError, fmt, std::forward<Args>(args)...);
}

inline void DEBUG(std::string_view msg) { detail::log_formatted(stdout, Level::kDebug, "{}", msg); }

template <typename... Args>
inline void DEBUG(fmt::format_string<Args...> fmt, Args&&... args) {
    detail::log_formatted(stdout, Level::kDebug, fmt, std::forward<Args>(args)...);
}

}  // namespace loglite::log

#endif  // LOGLITE_LOG_HPP_
