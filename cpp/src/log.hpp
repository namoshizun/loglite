#ifndef LOGLITE_LOG_HPP_
#define LOGLITE_LOG_HPP_

#include <chrono>
#include <cstdio>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <unistd.h>

namespace loglite::log {

enum class Level { kDebug, kInfo, kWarn, kError };

inline Level g_min_level = Level::kInfo;

inline void SetLevel(Level level) { g_min_level = level; }

namespace detail {
inline bool enabled(Level level) {
    return static_cast<int>(level) >= static_cast<int>(g_min_level);
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

inline void write(FILE* stream, Level level, std::string_view msg) {
    const auto ts = detail::timestamp();
    const auto label = detail::level_label(level);
    std::string line;
    if (stream_supports_color(stream)) {
        const auto levelStyled = fmt::format(detail::style_for(level), "{}", label);
        line = fmt::format("[{}] [{}] {}\n", ts, levelStyled, msg);
    } else {
        line = fmt::format("[{}] [{}] {}\n", ts, label, msg);
    }
    std::fwrite(line.data(), 1, line.size(), stream);
}
}  // namespace detail

inline void INFO(std::string_view msg) {
    if (!detail::enabled(Level::kInfo)) return;
    detail::write(stdout, Level::kInfo, msg);
}

inline void WARN(std::string_view msg) {
    if (!detail::enabled(Level::kWarn)) return;
    detail::write(stderr, Level::kWarn, msg);
}

inline void ERROR(std::string_view msg) {
    if (!detail::enabled(Level::kError)) return;
    detail::write(stderr, Level::kError, msg);
}

inline void DEBUG(std::string_view msg) {
    if (!detail::enabled(Level::kDebug)) return;
    detail::write(stdout, Level::kDebug, msg);
}

}  // namespace loglite::log

#endif  // LOGLITE_LOG_HPP_
