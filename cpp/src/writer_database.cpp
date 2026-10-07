#include "writer_database.hpp"

#include "log.hpp"
#include "migrations.hpp"
#include "utils.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <algorithm>
#include <iterator>
#include <ranges>
#include <vector>

namespace loglite {

WriterDatabase::WriterDatabase(const Config& cfg)
    : Database(cfg, std::make_shared<DatabaseCatalog>(cfg)) {}

void WriterDatabase::Open() { Open(cfg_.db_path); }

void WriterDatabase::Open(const std::filesystem::path& path) {
    note_path(path);
    ensure_ok(sqlite3_open(path.string().c_str(), &db_), "sqlite3_open");
    apply_params(AccessMode::WRITE);
    log::DEBUG("Opened writer SQLite connection: {}", path.string());
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
    Initialize(cfg_.auto_rollout ? std::span<const Migration>{cfg_.migrations}
                                 : std::span<const Migration>{});
}

void WriterDatabase::Initialize(std::span<const Migration> migrations) {
    CreateInternalTables();

    MigrationManager mgr{*this, migrations};
    while (mgr.ApplyPendingMigrations()) {
    }

    RefreshColumnInfo();
    LoadColumnDictionary();

    committed_id_ = catalog_->log_column_info.empty() ? 0 : GetMaxLogId();
}

void WriterDatabase::LoadColumnDictionary() {
    LookupTable lut;
    for (const auto& [col, value, id] : GetColumnDictRows()) {
        lut[col][value] = id;
    }

    log::INFO("Loaded column dictionary ({} entries)", lut.size());

    if (catalog_->col_dict) {
        catalog_->col_dict->Reload(std::move(lut));
        return;
    }

    catalog_->col_dict = std::make_shared<ColumnDictionary>(
        std::move(lut), [this](const std::string& col, const std::string& val, ValueId vid) {
            return InsertColumnDictValue(col, val, vid);
        });
}

namespace {

class ConstraintAbort : public std::exception {};

StorageFailure ClassifyStorage(const SqliteError& error) {
    switch (error.primary()) {
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return StorageFailure::kTransient;
    case SQLITE_FULL:
    case SQLITE_IOERR:
        return StorageFailure::kDiskFull;
    default:
        return StorageFailure::kInvariant;
    }
}

}  // namespace

int64_t WriterDatabase::InsertOne(const nlohmann::json& log, int64_t assigned_id) {
    std::vector<std::string> names;
    std::vector<nlohmann::json> values;
    if (assigned_id > 0) {
        names.emplace_back("id");
        values.emplace_back(assigned_id);
    }

    for (const auto& column : catalog_->log_column_info) {
        if (column.is_pk) continue;
        const auto it = log.find(column.name);
        if (it == log.end()) continue;

        nlohmann::json serialized = serialize_value(*it);
        if (catalog_->compressed_columns.contains(column.name) && !serialized.is_null()) {
            const std::string text =
                serialized.is_string() ? serialized.get<std::string>() : serialized.dump();
            serialized = catalog_->col_dict->GetOrCreate(column.name, text);
        }
        names.push_back(column.name);
        values.push_back(std::move(serialized));
    }

    if (names.empty()) throw std::runtime_error("log row has no insertable columns");

    const auto sql =
        fmt::format("INSERT INTO {} ({}) VALUES ({})", cfg_.log_table_name, fmt::join(names, ","),
                    fmt::join(std::vector<std::string_view>(names.size(), "?"), ","));
    Statement stmt{db_, sql};
    for (int i = 0; i < static_cast<int>(values.size()); ++i) bind_param(stmt, i + 1, values[i]);
    stmt.Step();
    return assigned_id > 0 ? assigned_id : static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
}

nlohmann::json WriterDatabase::ReadBack(int64_t id) const {
    const auto fields = pluck_column_names(catalog_->log_column_info);
    const auto rows = ReadLogs(fields, {{"id", "=", id}}, LogOrder::kIdAscending, 1, 0);
    if (rows.empty()) throw std::runtime_error("committed row disappeared before readback");
    return rows.front().values;
}

void WriterDatabase::RollbackBatch() {
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    if (cfg_.compression.enabled) LoadColumnDictionary();
}

WriterDatabase::BatchWrite WriterDatabase::WriteAll(std::span<const nlohmann::json> logs,
                                                    int64_t first_id) {
    exec_sql("BEGIN");
    std::vector<int64_t> ids;
    ids.reserve(logs.size());
    try {
        for (size_t index = 0; index < logs.size(); ++index) {
            const int64_t assigned = first_id > 0 ? first_id + static_cast<int64_t>(index) : 0;
            try {
                ids.push_back(InsertOne(logs[index], assigned));
            } catch (const SqliteError& error) {
                if (error.primary() == SQLITE_CONSTRAINT) throw ConstraintAbort{};
                throw;
            }
        }

        BatchWrite written;
        written.rows.resize(logs.size());
        for (size_t index = 0; index < ids.size(); ++index) {
            written.rows[index].disposition = RowWrite::Disposition::kCommitted;
            written.rows[index].id = ids[index];
            written.rows[index].stored = ReadBack(ids[index]);
        }
        exec_sql("COMMIT");
        if (!ids.empty()) committed_id_ = GetMaxLogId();
        return written;
    } catch (const ConstraintAbort&) {
        RollbackBatch();
        throw;
    } catch (...) {
        RollbackBatch();
        throw;
    }
}

WriterDatabase::BatchWrite WriterDatabase::WriteIsolated(std::span<const nlohmann::json> logs,
                                                         int64_t first_id) {
    BatchWrite written;
    written.rows.resize(logs.size());

    for (size_t index = 0; index < logs.size(); ++index) {
        const int64_t assigned = first_id > 0 ? first_id + static_cast<int64_t>(index) : 0;
        try {
            exec_sql("BEGIN");
            const int64_t id = InsertOne(logs[index], assigned);
            auto stored = ReadBack(id);
            exec_sql("COMMIT");
            committed_id_ = GetMaxLogId();
            written.rows[index].disposition = RowWrite::Disposition::kCommitted;
            written.rows[index].id = id;
            written.rows[index].stored = std::move(stored);
        } catch (const SqliteError& error) {
            RollbackBatch();
            if (error.primary() == SQLITE_CONSTRAINT) {
                written.rows[index].disposition = RowWrite::Disposition::kRejected;
                written.rows[index].reason = error.what();
                continue;
            }
            written.failure = ClassifyStorage(error);
            written.message = error.what();
            for (size_t rest = index; rest < logs.size(); ++rest)
                written.rows[rest].disposition = RowWrite::Disposition::kPending;
            return written;
        } catch (const std::exception& error) {
            RollbackBatch();
            written.failure = StorageFailure::kInvariant;
            written.message = error.what();
            for (size_t rest = index; rest < logs.size(); ++rest)
                written.rows[rest].disposition = RowWrite::Disposition::kPending;
            return written;
        }
    }
    return written;
}

WriterDatabase::BatchWrite WriterDatabase::WriteBatch(std::span<const nlohmann::json> logs,
                                                      int64_t first_id) {
    if (logs.empty()) return {};
    try {
        return WriteAll(logs, first_id);
    } catch (const ConstraintAbort&) {
        return WriteIsolated(logs, first_id);
    } catch (const SqliteError& error) {
        BatchWrite failed;
        failed.rows.resize(logs.size());
        failed.failure = ClassifyStorage(error);
        failed.message = error.what();
        for (auto& row : failed.rows) row.disposition = RowWrite::Disposition::kPending;
        if (failed.failure == StorageFailure::kTransient) return failed;
        throw StorageError{failed.failure, failed.message};
    }
}

int WriterDatabase::InsertRows(std::span<const nlohmann::json> logs, int64_t first_id) {
    const auto written = WriteBatch(logs, first_id);
    if (written.failure == StorageFailure::kTransient)
        throw std::runtime_error(written.message.empty() ? "storage is temporarily unavailable"
                                                         : written.message);
    if (written.failure != StorageFailure::kNone)
        throw StorageError{written.failure, written.message};

    int inserted = 0;
    for (const auto& row : written.rows)
        if (row.disposition == RowWrite::Disposition::kCommitted) ++inserted;
    return inserted;
}

int WriterDatabase::DeleteLogs(const std::vector<QueryFilter>& filters) {
    auto [where, params] = build_where_clause(filters);
    auto sql = fmt::format("DELETE FROM {} WHERE {}", cfg_.log_table_name, where);
    Statement stmt{db_, sql};

    for (int i = 1; const auto& value : params) bind_param(stmt, i++, value);
    stmt.Step();

    return sqlite3_changes(db_);
}

int WriterDatabase::DeleteOldLogs(std::string_view cutoff, int64_t limit) {
    Statement remove{
        db_, fmt::format(
                 "DELETE FROM {} WHERE id IN (SELECT id FROM {} WHERE {} <= ? ORDER BY id LIMIT ?)",
                 cfg_.log_table_name, cfg_.log_table_name, cfg_.log_timestamp_field)};
    bind_param(remove, 1, cutoff);
    bind_param(remove, 2, limit);
    remove.Step();
    return sqlite3_changes(db_);
}

int WriterDatabase::DeleteOldestLogs(int64_t count) {
    Statement remove{
        db_, fmt::format("DELETE FROM {} WHERE id IN (SELECT id FROM {} ORDER BY id LIMIT ?)",
                         cfg_.log_table_name, cfg_.log_table_name)};
    bind_param(remove, 1, count);
    remove.Step();
    return sqlite3_changes(db_);
}

std::optional<std::string> WriterDatabase::GetPartitionInterval() const {
    Statement table{
        db_, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'partition_state'"};
    if (table.Step() != SQLITE_ROW) return std::nullopt;

    Statement state{db_, "SELECT interval FROM partition_state WHERE id = 1"};
    if (state.Step() != SQLITE_ROW) return std::nullopt;
    return column_to_json(state, 0).get<std::string>();
}

void WriterDatabase::InitPartitionState(std::string_view interval) {
    exec_sql(
        "CREATE TABLE IF NOT EXISTS partition_state ("
        "id INTEGER PRIMARY KEY CHECK (id = 1), interval TEXT NOT NULL, "
        "reserved_id INTEGER NOT NULL)");
    Statement state{db_,
                    "INSERT INTO partition_state (id, interval, reserved_id) VALUES (1, ?, 0) "
                    "ON CONFLICT(id) DO NOTHING"};
    bind_param(state, 1, interval);
    state.Step();
}

int64_t WriterDatabase::ReserveLogIds(int64_t count) {
    Statement reserve{db_,
                      "UPDATE partition_state SET reserved_id = reserved_id + ? WHERE id = 1 "
                      "RETURNING reserved_id"};
    bind_param(reserve, 1, count);
    if (reserve.Step() != SQLITE_ROW) throw std::runtime_error("Missing partition ID allocator");

    const int64_t reserved = sqlite3_column_int64(reserve, 0);
    // Finish the statement so the reservation commits before the caller writes rows.
    reserve.Step();

    return reserved - count + 1;
}

void WriterDatabase::ObserveLogId(int64_t id) {
    Statement observe{db_,
                      "UPDATE partition_state SET reserved_id = MAX(reserved_id, ?) WHERE id = 1"};
    bind_param(observe, 1, id);
    observe.Step();
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

    stmt.Step();
    return true;
}

bool WriterDatabase::InsertDatabaseStats(const DatabaseStatsRow& row) {
    Statement stmt{db_,
                   "INSERT INTO database_stats (timestamp, rows_count, db_size) VALUES (?, ?, ?)"};

    sqlite3_bind_text(stmt, 1, row.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, row.rows_count);
    sqlite3_bind_int64(stmt, 3, row.db_size);

    stmt.Step();
    return true;
}

int WriterDatabase::DeleteStatsBefore(std::string_view cutoff) {
    int removed = 0;
    for (const auto* sql : {"DELETE FROM activity_stats WHERE until < ?",
                            "DELETE FROM database_stats WHERE timestamp < ?"}) {
        Statement stmt{db_, sql};
        sqlite3_bind_text(stmt, 1, cutoff.data(), static_cast<int>(cutoff.size()),
                          SQLITE_TRANSIENT);
        stmt.Step();
        removed += sqlite3_changes(db_);
    }
    return removed;
}

std::vector<int> WriterDatabase::GetAppliedVersions() const {
    Statement stmt{db_, "SELECT version FROM versions ORDER BY version"};

    std::vector<int> out;
    while (stmt.Step() == SQLITE_ROW) out.push_back(sqlite3_column_int(stmt, 0));
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
        ins.Step();
        exec_sql("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }

    log::INFO("Applied migration v{}", version);
    RefreshColumnInfo();
    return true;
}

bool WriterDatabase::RollbackMigration(int version, const std::vector<std::string>& statements) {
    if (!range_contains(GetAppliedVersions(), version)) return false;

    exec_sql("BEGIN");
    try {
        for (const auto& sql : statements) exec_sql(sql);
        Statement del{db_, "DELETE FROM versions WHERE version = ?"};
        sqlite3_bind_int(del, 1, version);
        del.Step();
        exec_sql("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }

    log::INFO("Rolled back migration v{}", version);
    RefreshColumnInfo();
    return true;
}

std::vector<std::tuple<std::string, std::string, ValueId>> WriterDatabase::GetColumnDictRows()
    const {
    Statement stmt{db_, "SELECT column, value, value_id FROM column_dictionary"};
    std::vector<std::tuple<std::string, std::string, ValueId>> rows;

    while (stmt.Step() == SQLITE_ROW) {
        const auto* col = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int vid = sqlite3_column_int(stmt, 2);

        rows.emplace_back(col ? std::string{col, static_cast<size_t>(sqlite3_column_bytes(stmt, 0))}
                              : std::string{},
                          val ? std::string{val, static_cast<size_t>(sqlite3_column_bytes(stmt, 1))}
                              : std::string{},
                          vid);
    }

    return rows;
}

bool WriterDatabase::InsertColumnDictValue(const std::string& col, const std::string& value,
                                           ValueId id) {
    Statement stmt{db_, "INSERT INTO column_dictionary (column, value, value_id) VALUES (?, ?, ?)"};
    sqlite3_bind_text(stmt, 1, col.data(), static_cast<int>(col.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);
    return stmt.Step() == SQLITE_DONE;
}

}  // namespace loglite
