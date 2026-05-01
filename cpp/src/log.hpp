#ifndef LOGLITE_LOG_HPP_
#define LOGLITE_LOG_HPP_

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

namespace loglite::log {

inline std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%S}", now);
}

inline void write(FILE* stream, std::string_view level, std::string_view msg) {
    auto line = std::format("[{}] [{}] {}\n", timestamp(), level, msg);
    std::fwrite(line.data(), 1, line.size(), stream);
}

inline void info(std::string_view msg) { write(stdout, "INFO ", msg); }

inline void warn(std::string_view msg) { write(stderr, "WARN ", msg); }

inline void error(std::string_view msg) { write(stderr, "ERROR", msg); }

inline void debug(std::string_view msg, bool enabled = true) {
    if (enabled) write(stdout, "DEBUG", msg);
}

}  // namespace loglite::log

#endif  // LOGLITE_LOG_HPP_
