#pragma once

#include "column_dict.hpp"
#include "config.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <memory>
#include <set>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace loglite {

// ── SQLite3 RAII wrappers ─────────────────────────────────────────────────────

// Prepared statement; automatically finalized on destruction.
struct Statement {
    sqlite3_stmt* raw{};

    Statement() = default;
    Statement(sqlite3* db, std::string_view sql);
    ~Statement() { sqlite3_finalize(raw); }

    Statement(const Statement&)            = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& o) noexcept : raw(std::exchange(o.raw, nullptr)) {}

    operator sqlite3_stmt*() const noexcept { return raw; }
};

// ── Database ──────────────────────────────────────────────────────────────────

class Database {
public:
    explicit Database(const Config& cfg);
    ~Database();

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    // Open the connection and apply PRAGMAs.
    void open();
    void close();

    // Create only the internal loglite tables (versions, column_dictionary).
    // Idempotent.  Called by both initialize() and the migration CLI commands.
    void create_internal_tables();

    // Create internal tables, apply auto-migrations, load schema + dict.
    void initialize();

    // ── Schema ────────────────────────────────────────────────────────────────

    [[nodiscard]] const std::vector<ColumnInfo>& column_info() const;
    void refresh_column_info();

    // ── CRUD ──────────────────────────────────────────────────────────────────

    // Returns number of rows actually inserted.
    int insert(const std::vector<nlohmann::json>& logs);

    PaginatedQueryResult query(
        const std::vector<std::string>& fields,
        const std::vector<QueryFilter>& filters,
        int limit,
        int offset) const;

    int delete_logs(const std::vector<QueryFilter>& filters);

    // ── Aggregate helpers ─────────────────────────────────────────────────────

    int64_t get_max_log_id() const;
    int64_t get_min_log_id() const;
    std::string get_min_timestamp() const; // ISO-8601 string

    // ── SQLite PRAGMAs ────────────────────────────────────────────────────────

    std::string get_pragma(std::string_view name) const;
    void        set_pragma(std::string_view name, std::string_view value);
    void        incremental_vacuum(int page_count);
    void        vacuum();
    void        wal_checkpoint(std::string_view mode = "TRUNCATE");
    double      get_size_mb() const;

    // ── Migrations ────────────────────────────────────────────────────────────

    std::vector<int> get_applied_versions() const;
    bool apply_migration(int version, const std::vector<std::string>& statements);
    bool rollback_migration(int version, const std::vector<std::string>& statements);

    // ── Column dictionary ─────────────────────────────────────────────────────

    std::vector<std::tuple<std::string, std::string, ValueId>> get_column_dict_rows() const;
    void insert_column_dict_value(const std::string& col, const std::string& value, ValueId id);

    // ── Health ────────────────────────────────────────────────────────────────

    bool ping() const;

private:
    // Build the WHERE clause + params vector from a filter list.
    // Returns {"1=0", {}} immediately when a compressed-column filter has no
    // candidates (short-circuit: guaranteed 0 results).
    struct WhereClause { std::string sql; std::vector<nlohmann::json> params; };
    WhereClause build_where_clause(const std::vector<QueryFilter>& filters) const;

    const Config&                  cfg_;
    sqlite3*                       db_{};
    mutable std::vector<ColumnInfo> column_info_;
    std::set<std::string>          compressed_columns_;
    std::unique_ptr<ColumnDictionary> col_dict_;
};

} // namespace loglite
