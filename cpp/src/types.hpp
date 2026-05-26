#ifndef LOGLITE_TYPES_HPP_
#define LOGLITE_TYPES_HPP_

#include <boost/describe.hpp>

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

    nlohmann::json ToJSON() {
        auto obj = nlohmann::json::object();
        obj["total"] = total;
        obj["offset"] = offset;
        obj["limit"] = limit;
        obj["results"] = std::move(results);
        return obj;
    }
};

// ── Internal stats rows ───────────────────────────────────────────────────────

struct ActivityStatsRow {
    std::string since;
    std::string until;
    int64_t query_count{};
    int64_t query_min{};
    int64_t query_max{};
    int64_t query_avg{};
    int64_t ingest_count{};
    int64_t ingest_size_min{};
    int64_t ingest_size_max{};
    int64_t ingest_size_avg{};
    int64_t ingest_drop_count{};
    int64_t insert_batch_count{};
    int64_t insert_total_count{};
    int64_t insert_total_cost{};
    int64_t sse_session_count{};
    int64_t http_conn_count{};
};

struct DatabaseStatsRow {
    std::string timestamp;
    int64_t rows_count{};
    int64_t db_size{};
};

// ── Stats query result ─────────────────────────────────────────────────────────

struct StatsQueryResult {
    std::vector<std::string> fields;
    std::vector<std::vector<nlohmann::json>> data;
};

// Boost.Describe — metadata for (de)serialization and config loading (see config.cpp).
BOOST_DESCRIBE_STRUCT(Migration, (), (version, rollout, rollback))
BOOST_DESCRIBE_STRUCT(CompressionConfig, (), (enabled, columns))

}  // namespace loglite

#endif  // LOGLITE_TYPES_HPP_
