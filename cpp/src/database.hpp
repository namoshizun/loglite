#ifndef LOGLITE_DATABASE_HPP_
#define LOGLITE_DATABASE_HPP_

#include "config.hpp"
#include "types.hpp"
#include "column_dict.hpp"

#include <memory>
#include <sqlite3.h>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace loglite {

// Shared schema + column dictionary across writer and read-pool connections.
// Populated by the writer during Initialize(); immutable schema for server lifetime.
struct DatabaseCatalog {
    explicit DatabaseCatalog(const Config& cfg) : cfg(cfg) {
        if (cfg.compression.enabled) {
            for (const auto& c : cfg.compression.columns) compressed_columns.insert(c);
        }
    }

    const Config& cfg;
    std::set<std::string> compressed_columns;
    std::vector<ColumnInfo> log_column_info;
    std::vector<ColumnInfo> activity_stats_column_info;
    std::vector<ColumnInfo> db_stats_column_info;
    std::shared_ptr<ColumnDictionary> col_dict;
};

struct Statement {
    sqlite3_stmt* raw{};

    Statement() = default;
    Statement(sqlite3* db, std::string_view sql);
    ~Statement() { sqlite3_finalize(raw); }

    Statement(Statement&& o) noexcept : raw(std::exchange(o.raw, nullptr)) {}
    operator sqlite3_stmt*() const noexcept { return raw; }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

// Shared connection + catalog; subclass for read vs write APIs.
class Database {
   public:
    virtual ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void Close();

    // DB query helpers
    [[nodiscard]] std::vector<ColumnInfo> FetchTableColumns(std::string_view table_name) const;
    [[nodiscard]] int64_t EstimateLogRowCount() const;

    [[nodiscard]] std::shared_ptr<DatabaseCatalog> catalog() const { return catalog_; }

   protected:
    struct WhereClause {
        std::string sql;
        std::vector<nlohmann::json> params;
    };

    enum class AccessMode {
        READ,
        WRITE,
    };

    Database(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog);

    [[nodiscard]] sqlite3* connection() const noexcept { return db_; }
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }

    [[nodiscard]] WhereClause build_where_clause(const std::vector<QueryFilter>& filters) const;
    void validate_field(std::string_view name) const;

    // SQLite param helpers
    void apply_params(AccessMode mode);
    void set_pragma(std::string_view name, std::string_view value);
    [[nodiscard]] std::string get_pragma(std::string_view name) const;

    // Generic helpers
    void exec_sql(std::string_view sql) const;
    void ensure_ok(int rc, std::string_view ctx) const;
    static void bind_param(sqlite3_stmt* stmt, int idx, const nlohmann::json& v);
    [[nodiscard]] static nlohmann::json column_to_json(sqlite3_stmt* stmt, int col);
    [[nodiscard]] static nlohmann::json serialize_value(const nlohmann::json& v);
    [[nodiscard]] static std::vector<std::string> pluck_column_names(
        const std::vector<ColumnInfo>& infos);

    const Config& cfg_;
    sqlite3* db_{};
    std::shared_ptr<DatabaseCatalog> catalog_;
};

}  // namespace loglite

#endif  // LOGLITE_DATABASE_HPP_
