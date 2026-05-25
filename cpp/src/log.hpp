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
    return isatty(fileno(stream)) != 0;
}

inline void write(FILE* stream, Level level, std::string_view msg) {
    const auto ts = timestamp();
    const auto label = level_label(level);
    std::string line;
    if (stream_supports_color(stream)) {
        line = fmt::format("[{}] [{}] {}\n", ts, fmt::format(style_for(level), "{}", label), msg);
    } else {
        line = fmt::format("[{}] [{}] {}\n", ts, label, msg);
    }
    std::fwrite(line.data(), 1, line.size(), stream);
}

inline void info(std::string_view msg) { write(stdout, Level::kInfo, msg); }

inline void warn(std::string_view msg) { write(stderr, Level::kWarn, msg); }

inline void error(std::string_view msg) { write(stderr, Level::kError, msg); }

inline void debug(std::string_view msg, bool enabled = true) {
    if (enabled) write(stdout, Level::kDebug, msg);
}

}  // namespace loglite::log

#endif  // LOGLITE_LOG_HPP_
