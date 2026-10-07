#ifndef LOGLITE_DATABASE_HPP_
#define LOGLITE_DATABASE_HPP_

#include "config.hpp"
#include "types.hpp"
#include "column_dict.hpp"

#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace loglite {

// Expand and validate a projection using only the immutable in-memory schema.
[[nodiscard]] std::vector<std::string> ResolveLogFields(std::span<const ColumnInfo> schema,
                                                        const std::vector<std::string>& fields);

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

class SqliteError : public std::runtime_error {
   public:
    SqliteError(int code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    [[nodiscard]] int code() const noexcept { return code_; }
    [[nodiscard]] int primary() const noexcept { return code_ & 0xff; }

   private:
    int code_;
};

// Physical file usage. `occupied_bytes` excludes free pages; `allocated_bytes`
// is the main file; WAL and shared-memory files are counted separately.
struct StorageFootprint {
    int64_t occupied_bytes{};
    int64_t allocated_bytes{};
    int64_t wal_bytes{};
    int64_t shm_bytes{};
    int64_t total_bytes{};
};

struct Statement {
    sqlite3_stmt* raw{};

    Statement() = default;
    Statement(sqlite3* db, std::string_view sql);
    ~Statement() { sqlite3_finalize(raw); }

    Statement(Statement&& o) noexcept : raw(std::exchange(o.raw, nullptr)) {}
    operator sqlite3_stmt*() const noexcept { return raw; }

    // Return SQLITE_ROW or SQLITE_DONE; every other result is a database error.
    int Step();

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
    void RefreshColumnInfo();

    // DB query helpers
    [[nodiscard]] std::vector<ColumnInfo> FetchTableColumns(std::string_view table_name) const;
    [[nodiscard]] int64_t EstimateLogRowCount() const;
    [[nodiscard]] int64_t CountLogRows() const;
    [[nodiscard]] int64_t EstimateAvgRowBytes() const;
    [[nodiscard]] std::shared_ptr<DatabaseCatalog> catalog() const { return catalog_; }
    [[nodiscard]] int64_t GetSizeBytes() const;
    [[nodiscard]] StorageFootprint Footprint() const;
    [[nodiscard]] double GetSizeMB() const;
    [[nodiscard]] std::string GetPragma(std::string_view name) const;
    [[nodiscard]] int64_t GetMaxLogId() const;
    [[nodiscard]] int64_t GetMinLogId() const;
    [[nodiscard]] std::string GetMinTimestamp() const;
    [[nodiscard]] const std::vector<ColumnInfo>& GetColumnInfo() const;
    void ValidateFilters(const std::vector<QueryFilter>& filters) const;

    enum class LogOrder { kNewestFirst, kIdAscending, kIdDescending };

    struct LogRow {
        int64_t id{};
        nlohmann::json values;
    };

    // `fields` must be resolved. Negative limits follow SQLite: no limit.
    [[nodiscard]] std::vector<LogRow> ReadLogs(const std::vector<std::string>& fields,
                                               const std::vector<QueryFilter>& filters,
                                               LogOrder order, int limit, int offset) const;

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

    // Generic helpers
    void exec_sql(std::string_view sql) const;
    void ensure_ok(int rc, std::string_view ctx) const;
    static void bind_param(sqlite3_stmt* stmt, int idx, const nlohmann::json& v);
    [[nodiscard]] static nlohmann::json column_to_json(sqlite3_stmt* stmt, int col);
    [[nodiscard]] nlohmann::json DecodeRow(sqlite3_stmt* stmt,
                                           const std::vector<std::string>& fields) const;
    [[nodiscard]] static nlohmann::json serialize_value(const nlohmann::json& v);
    [[nodiscard]] static std::vector<std::string> pluck_column_names(
        const std::vector<ColumnInfo>& infos);
    [[nodiscard]] const ColumnDictionary* dictionary() const;
    void note_path(const std::filesystem::path& path) { path_ = path; }

    const Config& cfg_;
    sqlite3* db_{};
    std::filesystem::path path_;
    std::shared_ptr<DatabaseCatalog> catalog_;
    // Reader-local dictionary from the same snapshot as the rows being decoded.
    mutable std::shared_ptr<ColumnDictionary> snapshot_dict_;
};

}  // namespace loglite

#endif  // LOGLITE_DATABASE_HPP_
