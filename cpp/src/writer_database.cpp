#include "writer_database.hpp"

#include "log.hpp"
#include "migrations.hpp"
#include "utils.hpp"

#include <fmt/format.h>
#include <iterator>
#include <ranges>

namespace loglite {

WriterDatabase::WriterDatabase(const Config& cfg)
    : Database(cfg, std::make_shared<DatabaseCatalog>(cfg)) {}

void WriterDatabase::Open() {
    auto path = cfg_.db_path.string();
    ensure_ok(sqlite3_open(path.c_str(), &db_), "sqlite3_open");
    apply_params(AccessMode::WRITE);
    log::DEBUG("Opened writer SQLite connection: {}", path);
}

void WriterDatabase::CreateInternalTables() {
    exec_sql(R"(CREATE TABLE IF NOT EXISTS versions (
        version    INTEGER PRIMARY KEY,
        applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))");

    exec_sql(R"(CREATE TABLE IF NOT EXISTS column_dictionary (
        id       INTEGER PRIMARY KEY AUTOINCREMENT,
        column   TEXT    NOT NULL,
        value_id INTEGER NOT NULL,
        value    JSON
    ))");
    exec_sql(R"(CREATE TABLE IF NOT EXISTS activity_stats (
        id                  INTEGER PRIMARY KEY,
        since               DATETIME NOT NULL,
        until               DATETIME NOT NULL,
        query_count         INTEGER,
        query_min           INTEGER,
        query_max           INTEGER,
        query_avg           INTEGER,
        ingest_count        INTEGER,
        ingest_size_min     INTEGER,
        ingest_size_max     INTEGER,
        ingest_size_avg     INTEGER,
        ingest_drop_count   INTEGER,
        insert_batch_count  INTEGER,
        insert_total_count  INTEGER,
        insert_total_cost   INTEGER,
        sse_session_count   INTEGER,
        http_conn_count     INTEGER
    ))");
    exec_sql(R"(CREATE TABLE IF NOT EXISTS database_stats (
        id           INTEGER PRIMARY KEY,
        timestamp    DATETIME,
        rows_count   INTEGER,
        db_size      INTEGER
    ))");
}

void WriterDatabase::Initialize() {
    CreateInternalTables();

    if (cfg_.auto_rollout) {
        MigrationManager mgr{*this, cfg_.migrations};
        while (mgr.ApplyPendingMigrations()) {
        }
    }

    RefreshColumnInfo();

    LookupTable lut;
    for (const auto& [col, value, id] : GetColumnDictRows()) {
        lut[col][value] = id;
    }
    log::INFO("Loaded column dictionary ({} entries)", lut.size());

    catalog_->col_dict = std::make_shared<ColumnDictionary>(
        std::move(lut), [this](const std::string& col, const std::string& val, ValueId vid) {
            return InsertColumnDictValue(col, val, vid);
        });
}

const std::vector<ColumnInfo>& WriterDatabase::GetColumnInfo() const {
    return catalog_->log_column_info;
}

void WriterDatabase::RefreshColumnInfo() {
    catalog_->log_column_info = FetchTableColumns(cfg_.log_table_name);
    catalog_->activity_stats_column_info = FetchTableColumns("activity_stats");
    catalog_->db_stats_column_info = FetchTableColumns("database_stats");
}

int WriterDatabase::Insert(const std::vector<nlohmann::json>& logs) {
    std::vector<ColumnInfo> cols;
    cols.reserve(catalog_->log_column_info.size());
    std::ranges::copy_if(catalog_->log_column_info, std::back_inserter(cols),
                         [](const ColumnInfo& ci) { return !ci.is_pk; });

    if (cols.empty() || logs.empty()) return 0;

    std::string col_list, placeholders;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i) {
            col_list += ",";
            placeholders += ",";
        }
        col_list += cols[i].name;
        placeholders += "?";
    }
    auto sql =
        fmt::format("INSERT INTO {} ({}) VALUES ({})", cfg_.log_table_name, col_list, placeholders);
    Statement stmt{db_, sql};

    exec_sql("BEGIN");
    try {
        int inserted = 0;
        for (const auto& log : logs) {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);

            bool valid = true;
            for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
                const auto& ci = cols[i];
                auto it = log.find(ci.name);
                nlohmann::json raw = (it != log.end()) ? *it : nlohmann::json(nullptr);

                if (ci.not_null && raw.is_null()) {
                    log::WARN("Skipping log: column '{}' required but missing", ci.name);
                    valid = false;
                    break;
                }

                nlohmann::json serialized = serialize_value(raw);
                if (catalog_->compressed_columns.contains(ci.name) && !serialized.is_null()) {
                    std::string sv =
                        serialized.is_string() ? serialized.get<std::string>() : serialized.dump();
                    serialized = catalog_->col_dict->GetOrCreate(ci.name, sv);
                }
                bind_param(stmt, i + 1, serialized);
            }

            if (!valid) continue;
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                ++inserted;
            else
                log::ERROR("Insert step failed: {}", sqlite3_errmsg(db_));
        }
        exec_sql("COMMIT");
        return inserted;
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

int WriterDatabase::DeleteLogs(const std::vector<QueryFilter>& filters) {
    auto [where, params] = build_where_clause(filters);
    auto sql = fmt::format("DELETE FROM {} WHERE {}", cfg_.log_table_name, where);
    Statement stmt{db_, sql};
    for (int i = 0; i < static_cast<int>(params.size()); ++i) bind_param(stmt, i + 1, params[i]);
    ensure_ok(sqlite3_step(stmt), "delete_logs");
    return sqlite3_changes(db_);
}

int64_t WriterDatabase::GetMaxLogId() const {
    auto sql = fmt::format("SELECT MAX(id) FROM {}", cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        return sqlite3_column_int64(stmt, 0);
    return 0;
}

int64_t WriterDatabase::GetMinLogId() const {
    auto sql = fmt::format("SELECT MIN(id) FROM {}", cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        return sqlite3_column_int64(stmt, 0);
    return 0;
}

std::string WriterDatabase::GetMinTimestamp() const {
    auto sql = fmt::format("SELECT MIN({}) FROM {}", cfg_.log_timestamp_field, cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        return txt ? txt : "";
    }
    return "";
}

void WriterDatabase::SetPragma(std::string_view name, std::string_view value) {
    log::INFO(" PRAGMA {}={}", name, value);
    set_pragma(name, value);
}

void WriterDatabase::IncrementalVacuum(int page_count) {
    exec_sql(fmt::format("PRAGMA incremental_vacuum({})", page_count));
}

void WriterDatabase::Vacuum() { exec_sql("VACUUM"); }

void WriterDatabase::WALCheckpoint(std::string_view mode) {
    exec_sql(fmt::format("PRAGMA wal_checkpoint({})", mode));
}

bool WriterDatabase::InsertActivityStats(const ActivityStatsRow& row) {
    Statement stmt{db_, R"(INSERT INTO activity_stats (
        since, until,
        query_count, query_min, query_max, query_avg,
        ingest_count, ingest_size_min, ingest_size_max, ingest_size_avg, ingest_drop_count,
        insert_batch_count, insert_total_count, insert_total_cost,
        sse_session_count, http_conn_count
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))"};

    sqlite3_bind_text(stmt, 1, row.since.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row.until.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, row.query_count);
    sqlite3_bind_int64(stmt, 4, row.query_min);
    sqlite3_bind_int64(stmt, 5, row.query_max);
    sqlite3_bind_int64(stmt, 6, row.query_avg);
    sqlite3_bind_int64(stmt, 7, row.ingest_count);
    sqlite3_bind_int64(stmt, 8, row.ingest_size_min);
    sqlite3_bind_int64(stmt, 9, row.ingest_size_max);
    sqlite3_bind_int64(stmt, 10, row.ingest_size_avg);
    sqlite3_bind_int64(stmt, 11, row.ingest_drop_count);
    sqlite3_bind_int64(stmt, 12, row.insert_batch_count);
    sqlite3_bind_int64(stmt, 13, row.insert_total_count);
    sqlite3_bind_int64(stmt, 14, row.insert_total_cost);
    sqlite3_bind_int64(stmt, 15, row.sse_session_count);
    sqlite3_bind_int64(stmt, 16, row.http_conn_count);

    ensure_ok(sqlite3_step(stmt), "insert_activity_stats");
    return true;
}

bool WriterDatabase::InsertDatabaseStats(const DatabaseStatsRow& row) {
    Statement stmt{db_,
                   "INSERT INTO database_stats (timestamp, rows_count, db_size) VALUES (?, ?, ?)"};

    sqlite3_bind_text(stmt, 1, row.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, row.rows_count);
    sqlite3_bind_int64(stmt, 3, row.db_size);

    ensure_ok(sqlite3_step(stmt), "insert_database_stats");
    return true;
}

int WriterDatabase::DeleteStatsBefore(std::string_view cutoff) {
    int removed = 0;
    {
        Statement stmt{db_, "DELETE FROM activity_stats WHERE until < ?"};
        sqlite3_bind_text(stmt, 1, cutoff.data(), static_cast<int>(cutoff.size()),
                          SQLITE_TRANSIENT);
        ensure_ok(sqlite3_step(stmt), "delete_activity_stats");
        removed += sqlite3_changes(db_);
    }
    {
        Statement stmt{db_, "DELETE FROM database_stats WHERE timestamp < ?"};
        sqlite3_bind_text(stmt, 1, cutoff.data(), static_cast<int>(cutoff.size()),
                          SQLITE_TRANSIENT);
        ensure_ok(sqlite3_step(stmt), "delete_database_stats");
        removed += sqlite3_changes(db_);
    }
    return removed;
}

std::vector<int> WriterDatabase::GetAppliedVersions() const {
    Statement stmt{db_, "SELECT version FROM versions ORDER BY version"};
    std::vector<int> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(sqlite3_column_int(stmt, 0));
    return out;
}

bool WriterDatabase::ApplyMigration(int version, const std::vector<std::string>& statements) {
    auto applied = GetAppliedVersions();
    if (range_contains(applied, version)) {
        log::INFO("Migration v{} already applied", version);
        return true;
    }

    exec_sql("BEGIN");
    try {
        for (const auto& sql : statements) exec_sql(sql);
        Statement ins{db_, "INSERT INTO versions (version) VALUES (?)"};
        sqlite3_bind_int(ins, 1, version);
        sqlite3_step(ins);
        exec_sql("COMMIT");
        log::INFO("Applied migration v{}", version);
        RefreshColumnInfo();
        return true;
    } catch (const std::exception& e) {
        exec_sql("ROLLBACK");
        log::ERROR("Failed to apply migration v{}: {}", version, e.what());
        return false;
    }
}

bool WriterDatabase::RollbackMigration(int version, const std::vector<std::string>& statements) {
    exec_sql("BEGIN");
    try {
        for (const auto& sql : statements) exec_sql(sql);
        Statement del{db_, "DELETE FROM versions WHERE version = ?"};
        sqlite3_bind_int(del, 1, version);
        sqlite3_step(del);
        exec_sql("COMMIT");
        log::INFO("Rolled back migration v{}", version);
        RefreshColumnInfo();
        return true;
    } catch (const std::exception& e) {
        exec_sql("ROLLBACK");
        log::ERROR("Failed to rollback migration v{}: {}", version, e.what());
        return false;
    }
}

std::vector<std::tuple<std::string, std::string, ValueId>> WriterDatabase::GetColumnDictRows()
    const {
    Statement stmt{db_, "SELECT column, value, value_id FROM column_dictionary"};
    std::vector<std::tuple<std::string, std::string, ValueId>> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* col = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int vid = sqlite3_column_int(stmt, 2);
        rows.emplace_back(col ? col : "", val ? val : "", vid);
    }
    return rows;
}

bool WriterDatabase::InsertColumnDictValue(const std::string& col, const std::string& value,
                                           ValueId id) {
    Statement stmt{db_, "INSERT INTO column_dictionary (column, value, value_id) VALUES (?, ?, ?)"};
    sqlite3_bind_text(stmt, 1, col.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);
    return sqlite3_step(stmt) == SQLITE_DONE;
}

}  // namespace loglite
