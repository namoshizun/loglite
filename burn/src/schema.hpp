#ifndef BURN_SCHEMA_HPP_
#define BURN_SCHEMA_HPP_

#include "config.hpp"
#include "http.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace burn {

enum class FieldKind {
    kMessage,
    kLevel,
    kTimestamp,
    kText8,
    kNumber,
    kNull,
    kBool,
    kDateTimeNow,
};

struct FieldSpec {
    std::string name;
    FieldKind kind{};
};

struct SchemaPlan {
    std::vector<FieldSpec> fields;
};

inline std::string NormalizeColumnKind(std::string_view sqlite_type, bool compressed) {
    if (compressed) return "text";

    auto end = sqlite_type.find('(');
    if (end == std::string_view::npos) end = sqlite_type.size();
    std::string token(sqlite_type.substr(0, end));
    std::ranges::transform(token, token.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (token.starts_with("INT")) return "integer";
    if (token == "REAL" || token == "FLOAT" || token == "DOUBLE" || token == "NUMERIC" ||
        token == "DECIMAL")
        return "number";
    if (token == "TEXT" || token == "CHAR" || token == "CLOB" || token == "VARCHAR") return "text";
    if (token == "DATETIME" || token == "DATE" || token == "TIME") return "datetime";
    if (token == "JSON") return "json";
    if (token == "BLOB") return "blob";
    if (token == "BOOLEAN") return "boolean";
    return "text";
}

inline FieldKind KindFromSchema(std::string_view kind) {
    if (kind == "integer" || kind == "number") return FieldKind::kNumber;
    if (kind == "json" || kind == "blob") return FieldKind::kNull;
    if (kind == "boolean") return FieldKind::kBool;
    if (kind == "datetime") return FieldKind::kDateTimeNow;
    return FieldKind::kText8;
}

inline void RequireColumn(const nlohmann::json& columns, std::string_view name) {
    for (const auto& col : columns) {
        if (col.at("name").get<std::string>() == name) return;
    }
    throw std::runtime_error(std::format("schema missing required column '{}'", name));
}

inline SchemaPlan FetchSchemaPlan(const Endpoint& ep) {
    auto res = HttpGetSync(ep, "/schema");
    if (res.result() != http::status::ok) {
        throw std::runtime_error(
            std::format("GET /schema failed: HTTP {}", static_cast<unsigned>(res.result())));
    }

    auto body = nlohmann::json::parse(res.body());
    const auto& columns = body.at("columns");
    RequireColumn(columns, "timestamp");
    RequireColumn(columns, "message");
    RequireColumn(columns, "level");

    SchemaPlan plan;
    for (const auto& col : columns) {
        const auto name = col.at("name").get<std::string>();
        const bool pk = col.value("primary_key", false);
        if (pk) continue;

        const bool compressed = col.value("compressed", false);
        const bool not_null = col.value("not_null", false);
        const std::string sqlite_type = col.value("sqlite_type", std::string{"TEXT"});
        const std::string kind = NormalizeColumnKind(sqlite_type, compressed);

        if (name == "message") {
            plan.fields.push_back({name, FieldKind::kMessage});
            continue;
        }
        if (name == "level") {
            plan.fields.push_back({name, FieldKind::kLevel});
            continue;
        }
        if (name == "timestamp") {
            plan.fields.push_back({name, FieldKind::kTimestamp});
            continue;
        }
        if (compressed) {
            if (not_null) {
                plan.fields.push_back({name, FieldKind::kText8});
            }
            continue;
        }

        plan.fields.push_back({name, KindFromSchema(kind)});
    }
    return plan;
}

inline std::string UtcNowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z", tm.tm_year + 1900,
                       tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

inline std::string RandomText(std::mt19937_64& rng, std::size_t len) {
    static constexpr char kAlphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<std::size_t> dist(0, sizeof(kAlphabet) - 2);
    std::string out(len, 'x');
    for (std::size_t i = 0; i < len; ++i) {
        out[i] = kAlphabet[dist(rng)];
    }
    return out;
}

inline nlohmann::json BuildLogRecord(const SchemaPlan& plan, const Config& cfg,
                                     std::mt19937_64& rng) {
    nlohmann::json row = nlohmann::json::object();
    const double sigma = std::max(1.0, static_cast<double>(cfg.message_size_mean) / 4.0);
    std::normal_distribution<double> msg_len(static_cast<double>(cfg.message_size_mean), sigma);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int> num_dist(10, 500);
    static constexpr const char* kNonInfoLevels[] = {"DEBUG", "WARNING", "ERROR", "CRITICAL"};
    std::uniform_int_distribution<int> level_dist(0, 3);

    for (const auto& field : plan.fields) {
        switch (field.kind) {
        case FieldKind::kMessage: {
            const auto len = static_cast<std::size_t>(std::max(1.0, std::round(msg_len(rng))));
            row[field.name] = RandomText(rng, len);
            break;
        }
        case FieldKind::kLevel:
            if (unit(rng) < cfg.info_ratio) {
                row[field.name] = "INFO";
            } else {
                row[field.name] = kNonInfoLevels[level_dist(rng)];
            }
            break;
        case FieldKind::kTimestamp:
        case FieldKind::kDateTimeNow:
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
