#ifndef LOGLITE_DATABASE_HPP_
#define LOGLITE_DATABASE_HPP_

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

    Statement(Statement&& o) noexcept : raw(std::exchange(o.raw, nullptr)) {}
    operator sqlite3_stmt*() const noexcept { return raw; }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

// ── Database ──────────────────────────────────────────────────────────────────

class Database {
   public:
    explicit Database(const Config& cfg);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Open the connection and apply PRAGMAs.
    void Open();
    void Close();

    // Create only the internal loglite tables (versions, column_dictionary).
    // Idempotent.  Called by both Initialize() and the migration CLI commands.
    void CreateInternalTables();

    // Create internal tables, apply auto-migrations, load schema + dict.
    void Initialize();

    // ── Schema ────────────────────────────────────────────────────────────────
    [[nodiscard]] const std::vector<ColumnInfo>& GetColumnInfo() const;
    void RefreshColumnInfo();

    // ── CRUD ──────────────────────────────────────────────────────────────────

    // Returns number of rows actually inserted.
    int Insert(const std::vector<nlohmann::json>& logs);

    PaginatedQueryResult Query(const std::vector<std::string>& fields,
                               const std::vector<QueryFilter>& filters, int limit,
                               int offset) const;

    int DeleteLogs(const std::vector<QueryFilter>& filters);

    // ── Aggregate helpers ─────────────────────────────────────────────────────
    int64_t GetMaxLogId() const;
    int64_t GetMinLogId() const;
    std::string GetMinTimestamp() const;  // ISO-8601 string
    int64_t EstimateLogRowCount() const;

    // ── SQLite PRAGMAs ────────────────────────────────────────────────────────
    std::string GetPragma(std::string_view name) const;
    void SetPragma(std::string_view name, std::string_view value);
    void IncrementalVacuum(int page_count);
    void Vacuum();
    void WALCheckpoint(std::string_view mode = "TRUNCATE");
    int64_t GetSizeBytes() const;
    double GetSizeMB() const;

    // ── Internal stats ────────────────────────────────────────────────────────
    bool InsertActivityStats(const ActivityStatsRow& row);
    bool InsertDatabaseStats(const DatabaseStatsRow& row);
    int DeleteStatsBefore(std::string_view cutoff);

    StatsQueryResult QueryActivityStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const;
    StatsQueryResult QueryDatabaseStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const;

    // ── Migrations ────────────────────────────────────────────────────────────

    std::vector<int> GetAppliedVersions() const;
    bool ApplyMigration(int version, const std::vector<std::string>& statements);
    bool RollbackMigration(int version, const std::vector<std::string>& statements);

    // ── Column dictionary ─────────────────────────────────────────────────────

    std::vector<std::tuple<std::string, std::string, ValueId>> GetColumnDictRows() const;
    bool InsertColumnDictValue(const std::string& col, const std::string& value, ValueId id);

    // ── Health ────────────────────────────────────────────────────────────────

    bool Ping() const;

   private:
    // Build the WHERE clause + params vector from a filter list.
    // Returns {"1=0", {}} immediately when a compressed-column filter has no
    // candidates (short-circuit: guaranteed 0 results).
    struct WhereClause {
        std::string sql;
        std::vector<nlohmann::json> params;
    };
    WhereClause build_where_clause(const std::vector<QueryFilter>& filters) const;

    // Throws std::runtime_error if `name` is not a known column in column_info_.
    void validate_field(std::string_view name) const;

    static const std::vector<std::string>& activity_known_columns();
    static const std::vector<std::string>& database_known_columns();

    const Config& cfg_;
    sqlite3* db_{};
    mutable std::vector<ColumnInfo> column_info_;
    std::set<std::string> compressed_columns_;
    std::unique_ptr<ColumnDictionary> col_dict_;
};

}  // namespace loglite

#endif  // LOGLITE_DATABASE_HPP_
