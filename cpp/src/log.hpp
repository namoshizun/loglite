#pragma once

#include <chrono>
#include <format>
#include <print>
#include <string_view>

namespace loglite::log {

inline std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%S}", now);
}

inline void info(std::string_view msg) {
    std::println("[{}] [INFO ] {}", timestamp(), msg);
}

inline void warn(std::string_view msg) {
    std::println(stderr, "[{}] [WARN ] {}", timestamp(), msg);
}

inline void error(std::string_view msg) {
    std::println(stderr, "[{}] [ERROR] {}", timestamp(), msg);
}

inline void debug(std::string_view msg, bool enabled = true) {
    if (enabled)
        std::println("[{}] [DEBUG] {}", timestamp(), msg);
}

} // namespace loglite::log
