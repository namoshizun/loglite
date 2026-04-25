#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace loglite {

// ── Query filters ─────────────────────────────────────────────────────────────

// Operator literals matching the Python QueryOperator type alias.
// "~=" means substring match (translated to LIKE %value% in SQL).
using QueryOperator = std::string;  // one of: =  !=  >  >=  <  <=  ~=

struct QueryFilter {
    std::string field;
    QueryOperator op;
    nlohmann::json value;  // string, int64, double, or null
};

// ── Schema ────────────────────────────────────────────────────────────────────

struct ColumnInfo {
    std::string name;
    std::string type;
    bool not_null{false};
    bool is_pk{false};
};

// ── Migrations ────────────────────────────────────────────────────────────────

struct Migration {
    int version{};
    std::vector<std::string> rollout;
    std::vector<std::string> rollback;
};

// ── Compression ───────────────────────────────────────────────────────────────

struct CompressionConfig {
    bool enabled{false};
    std::vector<std::string> columns;
};

// ── Query result ──────────────────────────────────────────────────────────────

struct PaginatedQueryResult {
    int total{};
    int offset{};
    int limit{};
    std::vector<nlohmann::json> results;

    nlohmann::json to_json() const {
        return {
            {"total", total},
            {"offset", offset},
            {"limit", limit},
            {"results", results},
        };
    }
};

}  // namespace loglite
