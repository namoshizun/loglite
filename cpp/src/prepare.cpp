#include "prepare.hpp"

#include "utils.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

#include <fmt/format.h>

namespace loglite {

namespace {

std::optional<std::string> RejectValue(const nlohmann::json& value) {
    if (value.is_number_float() && !std::isfinite(value.get<double>()))
        return "numeric value is not finite";
    if (value.is_number_unsigned() &&
        value.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return "integer exceeds the representable range";
    if (value.is_object() || value.is_array()) return std::nullopt;
    return std::nullopt;
}

// Walks containers so a nested non-finite number is rejected with the row.
std::optional<std::string> RejectTree(const nlohmann::json& value) {
    if (auto reason = RejectValue(value)) return reason;
    if (value.is_structured()) {
        for (const auto& child : value) {
            if (auto reason = RejectTree(child)) return reason;
        }
    }
    return std::nullopt;
}

}  // namespace

Preparation PrepareEntries(const LogSchema& schema, const std::vector<nlohmann::json>& entries,
                           std::chrono::system_clock::time_point admitted_at) {
    Preparation out;
    const auto* timestamp = schema.Find(schema.timestamp_field());

    for (size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (!entry.is_object()) {
            out.rejected.push_back({index, "log entry must be a JSON object"});
            continue;
        }

        nlohmann::json fields = nlohmann::json::object();
        std::optional<std::string> invalid;
        for (const auto& [key, value] : entry.items()) {
            const auto* field = schema.Find(key);
            if (!field || !field->insertable) continue;
            if (auto reason = RejectTree(value)) {
                invalid = reason;
                break;
            }
            fields[key] = value;
        }
        if (invalid) {
            out.rejected.push_back({index, *invalid});
            continue;
        }

        bool required_missing = false;
        for (const auto& field : schema.fields()) {
            if (!field.insertable) continue;
            const bool present = fields.contains(field.name);
            if (!present) {
                if (field.not_null && !field.default_sql && !field.timestamp) {
                    out.rejected.push_back(
                        {index, fmt::format("column '{}' is required", field.name)});
                    required_missing = true;
                    break;
                }
                continue;
            }
            if (fields[field.name].is_null() && field.not_null && !field.default_sql) {
                out.rejected.push_back(
                    {index, fmt::format("column '{}' cannot be null", field.name)});
                required_missing = true;
                break;
            }
        }
        if (required_missing) continue;

        auto instant = admitted_at;
        if (timestamp) {
            const bool present = fields.contains(timestamp->name);
            std::optional<std::chrono::system_clock::time_point> parsed;
            if (present && fields[timestamp->name].is_string())
                parsed = parse_iso8601(fields[timestamp->name].get_ref<const std::string&>());

            if (schema.partitioned()) {
                instant =
                    std::chrono::floor<std::chrono::milliseconds>(parsed.value_or(admitted_at));
                fields[timestamp->name] = format_utc(instant);
            } else if (parsed) {
                instant = *parsed;
            }
        }

        PreparedEntry prepared;
        prepared.index = index;
        prepared.bytes = fields.dump().size();
        prepared.instant = instant;
        prepared.admitted_at = admitted_at;
        prepared.fields = std::move(fields);
        out.admitted.push_back(std::move(prepared));
    }

    return out;
}

}  // namespace loglite
