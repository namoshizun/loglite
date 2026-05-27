#ifndef LOGLITE_HANDLERS_SCHEMA_HPP_
#define LOGLITE_HANDLERS_SCHEMA_HPP_

#include "common.hpp"
#include "../context.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace loglite::handlers {

// UI-oriented column kind derived from SQLite declarative type (PRAGMA table_info).
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

inline nlohmann::json BuildSchemaPayload(const ServerContext& ctx) {
    const auto& cfg = ctx.config;
    const auto& columns = ctx.db_write.GetColumnInfo();
    const auto& compressed = ctx.db_write.catalog()->compressed_columns;

    nlohmann::json out_columns = nlohmann::json::array();
    for (const auto& ci : columns) {
        const bool is_compressed = cfg.compression.enabled && compressed.contains(ci.name);
        out_columns.push_back({{"name", ci.name},
                               {"kind", NormalizeColumnKind(ci.type, is_compressed)},
                               {"sqlite_type", ci.type},
                               {"compressed", is_compressed},
                               {"not_null", ci.not_null},
                               {"primary_key", ci.is_pk}});
    }

    return {{"table", cfg.log_table_name}, {"columns", std::move(out_columns)}};
}

template <class Body>
http::response<http::string_body> HandleSchema(const http::request<Body>& req, ServerContext& ctx) {
    return MakeOKResp(BuildSchemaPayload(ctx), req, ctx.config.allow_origin);
}

}  // namespace loglite::handlers

#endif  // LOGLITE_HANDLERS_SCHEMA_HPP_
