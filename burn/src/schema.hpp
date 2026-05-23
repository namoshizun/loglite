#ifndef BURN_SCHEMA_HPP_
#define BURN_SCHEMA_HPP_

#include "config.hpp"
#include "http.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <fmt/format.h>
#include <optional>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace burn {

enum class FieldKind {
    kMessage,
    kLevel,
    kUtcNow,
    kText8,
    kNumber,
    kNull,
    kBool,
};

struct FieldSpec {
    std::string name;
    FieldKind kind{};
};

struct SchemaPlan {
    std::vector<FieldSpec> fields;
};

[[nodiscard]] inline FieldKind KindFromSchema(std::string_view kind) {
    static constexpr std::pair<std::string_view, FieldKind> kKinds[] = {
        {"integer", FieldKind::kNumber}, {"number", FieldKind::kNumber},
        {"json", FieldKind::kNull},      {"blob", FieldKind::kNull},
        {"boolean", FieldKind::kBool},   {"datetime", FieldKind::kUtcNow},
        {"text", FieldKind::kText8},
    };
    const auto it = std::ranges::find(kKinds, kind, &std::pair<std::string_view, FieldKind>::first);
    return it != std::end(kKinds) ? it->second : FieldKind::kText8;
}

[[nodiscard]] inline bool HasColumn(const nlohmann::json& columns, std::string_view name) {
    return std::ranges::any_of(columns, [name](const nlohmann::json& col) {
        return col.at("name").get<std::string>() == name;
    });
}

inline void RequireColumns(const nlohmann::json& columns,
                           std::initializer_list<std::string_view> names) {
    for (const auto name : names) {
        if (!HasColumn(columns, name)) {
            throw std::runtime_error(fmt::format("schema missing required column '{}'", name));
        }
    }
}

[[nodiscard]] inline std::optional<FieldSpec> FieldFromSchemaColumn(const nlohmann::json& col) {
    if (col.value("primary_key", false)) {
        return std::nullopt;
    }

    const auto name = col.at("name").get<std::string>();
    const bool compressed = col.value("compressed", false);
    const bool not_null = col.value("not_null", false);
    const auto kind = col.at("kind").get<std::string_view>();

    if (name == "message") {
        return FieldSpec{name, FieldKind::kMessage};
    }
    if (name == "level") {
        return FieldSpec{name, FieldKind::kLevel};
    }
    if (name == "timestamp") {
        return FieldSpec{name, FieldKind::kUtcNow};
    }
    if (compressed) {
        return not_null ? std::optional{FieldSpec{name, FieldKind::kText8}} : std::nullopt;
    }
    return FieldSpec{name, KindFromSchema(kind)};
}

[[nodiscard]] inline SchemaPlan FetchSchemaPlan(const Endpoint& ep) {
    const auto res = HttpGetSync(ep, "/schema");
    if (res.result() != http::status::ok) {
        throw std::runtime_error(
            fmt::format("GET /schema failed: HTTP {}", static_cast<unsigned>(res.result())));
    }

    const auto body = nlohmann::json::parse(res.body());
    const auto& columns = body.at("columns");
    RequireColumns(columns, {"timestamp", "message", "level"});

    SchemaPlan plan;
    for (const auto& col : columns) {
        if (auto field = FieldFromSchemaColumn(col)) {
            plan.fields.push_back(std::move(*field));
        }
    }
    return plan;
}

[[nodiscard]] inline std::string UtcNowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    return fmt::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z", tm.tm_year + 1900,
                       tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

[[nodiscard]] inline std::string RandomText(std::mt19937_64& rng, std::size_t len) {
    static constexpr char kAlphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<std::size_t> dist(0, sizeof(kAlphabet) - 2);
    std::string out(len, '\0');
    std::ranges::generate(out, [&] { return kAlphabet[dist(rng)]; });
    return out;
}

[[nodiscard]] inline nlohmann::json BuildLogRecord(const SchemaPlan& plan, const Config& cfg,
                                                   std::mt19937_64& rng) {
    static constexpr std::array kNonInfoLevels = {"DEBUG", "WARNING", "ERROR", "CRITICAL"};

    const double sigma = std::max(1.0, static_cast<double>(cfg.message_size_mean) / 4.0);
    std::normal_distribution<double> msg_len(static_cast<double>(cfg.message_size_mean), sigma);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int> num_dist(10, 500);
    std::uniform_int_distribution<std::size_t> level_dist(0, kNonInfoLevels.size() - 1);

    nlohmann::json row = nlohmann::json::object();
    for (const auto& field : plan.fields) {
        switch (field.kind) {
        case FieldKind::kMessage: {
            const auto len = static_cast<std::size_t>(std::max(1.0, std::round(msg_len(rng))));
            row[field.name] = RandomText(rng, len);
            break;
        }
        case FieldKind::kLevel:
            row[field.name] = unit(rng) < cfg.info_ratio ? "INFO" : kNonInfoLevels[level_dist(rng)];
            break;
        case FieldKind::kUtcNow:
            row[field.name] = UtcNowIso8601();
            break;
        case FieldKind::kText8:
            row[field.name] = RandomText(rng, 8);
            break;
        case FieldKind::kNumber:
            row[field.name] = num_dist(rng);
            break;
        case FieldKind::kNull:
            row[field.name] = nullptr;
            break;
        case FieldKind::kBool:
            row[field.name] = unit(rng) < 0.5;
            break;
        }
    }
    return row;
}

}  // namespace burn

#endif
