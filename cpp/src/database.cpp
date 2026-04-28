#include "database.hpp"
#include "log.hpp"
#include "migrations.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

namespace loglite {

// ── Statement ──────────────────────────────────────────────────────────────────

Statement::Statement(sqlite3* db, std::string_view sql) {
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &raw, nullptr) !=
        SQLITE_OK)
        throw std::runtime_error(std::format("sqlite3_prepare_v2: {}", sqlite3_errmsg(db)));
}

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

void ensure_ok(int rc, sqlite3* db, std::string_view ctx) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
        throw std::runtime_error(std::format("{}: {}", ctx, sqlite3_errmsg(db)));
}

void exec_sql(sqlite3* db, std::string_view sql) {
    char* errmsg{};
    int rc = sqlite3_exec(db, std::string(sql).c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error(std::format("sqlite3_exec: {}", msg));
    }
}

void bind_param(sqlite3_stmt* stmt, int idx, const nlohmann::json& v) {
    if (v.is_null())
        sqlite3_bind_null(stmt, idx);
    else if (v.is_boolean())
        sqlite3_bind_int(stmt, idx, v.get<bool>() ? 1 : 0);
    else if (v.is_number_integer())
        sqlite3_bind_int64(stmt, idx, v.get<int64_t>());
    else if (v.is_number_float())
        sqlite3_bind_double(stmt, idx, v.get<double>());
    else {
        std::string s = v.is_string() ? v.get<std::string>() : v.dump();
        sqlite3_bind_text(stmt, idx, s.c_str(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
    }
}

nlohmann::json column_to_json(sqlite3_stmt* stmt, int col) {
    switch (sqlite3_column_type(stmt, col)) {
    case SQLITE_INTEGER:
        return sqlite3_column_int64(stmt, col);
    case SQLITE_FLOAT:
        return sqlite3_column_double(stmt, col);
    case SQLITE_TEXT: {
        const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return std::string{txt ? txt : ""};
    }
    case SQLITE_NULL:
        return nullptr;
    default:
        return nullptr;
    }
}

// Serialize a JSON log field value to a string/number suitable for storage.
nlohmann::json serialize_value(const nlohmann::json& v) {
    if (v.is_null()) return nullptr;
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if (v.is_number()) return v;
    if (v.is_string()) return v;
    // dict / array → JSON string
    return v.dump();
}

}  // namespace

// ── Database ──────────────────────────────────────────────────────────────────

Database::Database(const Config& cfg) : cfg_(cfg) {
    if (cfg_.compression.enabled) {
        for (const auto& c : cfg_.compression.columns) compressed_columns_.insert(c);
    }
}

Database::~Database() { Close(); }

void Database::Open() {
    auto path = cfg_.db_path.string();
    ensure_ok(sqlite3_open(path.c_str(), &db_), db_, "sqlite3_open");

    // Handle auto_vacuum mode change (requires VACUUM if mode differs).
    auto& params = cfg_.sqlite_params;
    if (auto it = params.find("auto_vacuum"); it != params.end()) {
        auto current = GetPragma("auto_vacuum");
        if (current != it->second) {
            SetPragma("auto_vacuum", it->second);
            exec_sql(db_, "VACUUM");
        }
    }

    // Apply remaining PRAGMAs.
    for (const auto& [k, v] : params) {
        if (k == "auto_vacuum") continue;
        SetPragma(k, v);
    }

    sqlite3_busy_timeout(db_, 5000);  // 5s busy timeout
    log::info(std::format("Opened SQLite database: {}", path));
}

void Database::Close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::CreateInternalTables() {
    exec_sql(db_,
             "CREATE TABLE IF NOT EXISTS versions ("
             "  version    INTEGER PRIMARY KEY,"
             "  applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
             ")");
    exec_sql(db_,
             "CREATE TABLE IF NOT EXISTS column_dictionary ("
             "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
             "  column   TEXT    NOT NULL,"
             "  value_id INTEGER NOT NULL,"
             "  value    JSON"
             ")");
}

void Database::Initialize() {
    CreateInternalTables();

    if (cfg_.auto_rollout) {
        MigrationManager mgr{*this, cfg_.migrations};
        while (mgr.ApplyPendingMigrations()) {
        }
    }

    RefreshColumnInfo();

    // Build ColumnDictionary from DB rows.
    LookupTable lut;
    for (const auto& [col, value, id] : GetColumnDictRows()) {
        lut[col][value] = id;
    }
    log::info(std::format("Loaded column dictionary ({} entries)", lut.size()));

    col_dict_ = std::make_unique<ColumnDictionary>(
        std::move(lut), [this](const std::string& col, const std::string& val, ValueId vid) {
            InsertColumnDictValue(col, val, vid);
        });
}

// ── Schema ────────────────────────────────────────────────────────────────────

const std::vector<ColumnInfo>& Database::GetColumnInfo() const { return column_info_; }

void Database::RefreshColumnInfo() {
    column_info_.clear();
    auto sql = std::format("PRAGMA table_info({})", cfg_.log_table_name);
    Statement stmt{db_, sql};
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ColumnInfo ci;
        ci.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        ci.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        ci.not_null = sqlite3_column_int(stmt, 3) != 0;
        ci.is_pk = sqlite3_column_int(stmt, 5) != 0;
        column_info_.push_back(std::move(ci));
    }
}

// ── WHERE clause builder ──────────────────────────────────────────────────────

Database::WhereClause Database::build_where_clause(const std::vector<QueryFilter>& filters) const {
    std::string sql_parts;
    std::vector<nlohmann::json> params;

    for (const auto& ft : filters) {
        if (!sql_parts.empty()) sql_parts += " AND ";

        if (compressed_columns_.contains(ft.field)) {
            auto ids = col_dict_->QueryCandidates(ft);
            if (ids.empty()) return {"1=0", {}};

            sql_parts += ft.field + " IN (";
            for (size_t i = 0; i < ids.size(); ++i) {
                sql_parts += (i ? ",?" : "?");
                params.push_back(ids[i]);
            }
            sql_parts += ")";
        } else if (ft.op == "~=") {
            sql_parts += ft.field + " LIKE ?";
            std::string fval = ft.value.is_string() ? ft.value.get<std::string>() : ft.value.dump();
            params.push_back("%" + fval + "%");
        } else {
            sql_parts += ft.field + " " + ft.op + " ?";
            params.push_back(ft.value);
        }
    }

    return {sql_parts.empty() ? "1=1" : sql_parts, std::move(params)};
}

// ── CRUD ──────────────────────────────────────────────────────────────────────

int Database::Insert(const std::vector<nlohmann::json>& logs) {
    // Columns excluding 'id' (auto-generated primary key).
    std::vector<ColumnInfo> cols;
    for (const auto& ci : column_info_)
        if (!ci.is_pk) cols.push_back(ci);

    if (cols.empty() || logs.empty()) return 0;

    // Build INSERT statement.
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
        std::format("INSERT INTO {} ({}) VALUES ({})", cfg_.log_table_name, col_list, placeholders);
    Statement stmt{db_, sql};

    exec_sql(db_, "BEGIN");
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
                log::warn(std::format("Skipping log: column '{}' required but missing", ci.name));
                valid = false;
                break;
            }

            nlohmann::json serialized = serialize_value(raw);
            if (compressed_columns_.contains(ci.name) && !serialized.is_null()) {
                std::string sv =
                    serialized.is_string() ? serialized.get<std::string>() : serialized.dump();
                serialized = col_dict_->GetOrCreate(ci.name, sv);
            }
            bind_param(stmt, i + 1, serialized);
        }

        if (!valid) continue;
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE)
            ++inserted;
        else
            log::error(std::format("Insert step failed: {}", sqlite3_errmsg(db_)));
    }
    exec_sql(db_, "COMMIT");
    return inserted;
}

PaginatedQueryResult Database::Query(const std::vector<std::string>& fields,
                                     const std::vector<QueryFilter>& filters, int limit,
                                     int offset) const {
    // Expand wildcard.
    std::vector<std::string> effective_fields;
    if (fields.size() == 1 && fields[0] == "*") {
        for (const auto& ci : column_info_) effective_fields.push_back(ci.name);
    } else {
        effective_fields.assign(fields.begin(), fields.end());
    }

    auto [where, params] = build_where_clause(filters);

    // Count total matching rows.
    auto count_sql = std::format("SELECT COUNT(id) FROM {} WHERE {}", cfg_.log_table_name, where);
    Statement count_stmt{db_, count_sql};
    for (int i = 0; i < static_cast<int>(params.size()); ++i)
        bind_param(count_stmt, i + 1, params[i]);

    int total = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) total = sqlite3_column_int(count_stmt, 0);

    if (total == 0) return {total, offset, limit, {}};

    // Fetch rows.
    std::string field_list;
    for (size_t i = 0; i < effective_fields.size(); ++i) {
        if (i) field_list += ",";
        field_list += effective_fields[i];
    }
    auto select_sql = std::format("SELECT {} FROM {} WHERE {} ORDER BY {} DESC LIMIT ? OFFSET ?",
                                  field_list, cfg_.log_table_name, where, cfg_.log_timestamp_field);

    Statement sel{db_, select_sql};
    int pi = 1;
    for (const auto& p : params) bind_param(sel, pi++, p);
    bind_param(sel, pi++, nlohmann::json(limit));
    bind_param(sel, pi++, nlohmann::json(offset));

    std::vector<nlohmann::json> results;
    results.reserve(static_cast<size_t>(limit));
    while (sqlite3_step(sel) == SQLITE_ROW) {
        nlohmann::json row;
        for (int c = 0; c < static_cast<int>(effective_fields.size()); ++c) {
            const auto& fname = effective_fields[c];
            auto val = column_to_json(sel, c);
            if (compressed_columns_.contains(fname) && val.is_number_integer()) {
                val = col_dict_->GetValue(fname, val.get<int>());
            }
            row[fname] = std::move(val);
        }
        results.push_back(std::move(row));
    }

    return {total, offset, limit, std::move(results)};
}

int Database::DeleteLogs(const std::vector<QueryFilter>& filters) {
    auto [where, params] = build_where_clause(filters);
    auto sql = std::format("DELETE FROM {} WHERE {}", cfg_.log_table_name, where);
    Statement stmt{db_, sql};
    for (int i = 0; i < static_cast<int>(params.size()); ++i) bind_param(stmt, i + 1, params[i]);
    ensure_ok(sqlite3_step(stmt), db_, "delete_logs");
    return sqlite3_changes(db_);
}

// ── Aggregate helpers ─────────────────────────────────────────────────────────

int64_t Database::GetMaxLogId() const {
    auto sql = std::format("SELECT MAX(id) FROM {}", cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        return sqlite3_column_int64(stmt, 0);
    return 0;
}

int64_t Database::GetMinLogId() const {
    auto sql = std::format("SELECT MIN(id) FROM {}", cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        return sqlite3_column_int64(stmt, 0);
    return 0;
}

std::string Database::GetMinTimestamp() const {
    auto sql = std::format("SELECT MIN({}) FROM {}", cfg_.log_timestamp_field, cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        return txt ? txt : "";
    }
    return "";
}

// ── PRAGMAs ───────────────────────────────────────────────────────────────────

std::string Database::GetPragma(std::string_view name) const {
    auto sql = std::format("PRAGMA {}", name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        return txt ? txt : "";
    }
    return "";
}

void Database::SetPragma(std::string_view name, std::string_view value) {
    exec_sql(db_, std::format("PRAGMA {}={}", name, value));
}

void Database::IncrementalVacuum(int page_count) {
    exec_sql(db_, std::format("PRAGMA incremental_vacuum({})", page_count));
}

void Database::Vacuum() { exec_sql(db_, "VACUUM"); }

void Database::WALCheckpoint(std::string_view mode) {
    exec_sql(db_, std::format("PRAGMA wal_checkpoint({})", mode));
}

double Database::GetSizeMB() const {
    int64_t page_count = std::stoll(GetPragma("page_count"));
    int64_t page_size = std::stoll(GetPragma("page_size"));
    int64_t freelist = std::stoll(GetPragma("freelist_count"));
    return bytes_to_mb((page_count - freelist) * page_size);
}

// ── Migrations ────────────────────────────────────────────────────────────────

std::vector<int> Database::GetAppliedVersions() const {
    Statement stmt{db_, "SELECT version FROM versions ORDER BY version"};
    std::vector<int> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(sqlite3_column_int(stmt, 0));
    return out;
}

bool Database::ApplyMigration(int version, const std::vector<std::string>& statements) {
    // Pre-check outside the transaction – safe to let this throw to the caller.
    auto applied = GetAppliedVersions();
    if (std::ranges::contains(applied, version)) {
        log::info(std::format("Migration v{} already applied", version));
        return true;
    }

    exec_sql(db_, "BEGIN");
    try {
        for (const auto& sql : statements) exec_sql(db_, sql);
        Statement ins{db_, "INSERT INTO versions (version) VALUES (?)"};
        sqlite3_bind_int(ins, 1, version);
        sqlite3_step(ins);
        exec_sql(db_, "COMMIT");
        log::info(std::format("Applied migration v{}", version));
        RefreshColumnInfo();
        return true;
    } catch (const std::exception& e) {
        exec_sql(db_, "ROLLBACK");
        log::error(std::format("Failed to apply migration v{}: {}", version, e.what()));
        return false;
    }
}

bool Database::RollbackMigration(int version, const std::vector<std::string>& statements) {
    exec_sql(db_, "BEGIN");
    try {
        for (const auto& sql : statements) exec_sql(db_, sql);
        Statement del{db_, "DELETE FROM versions WHERE version = ?"};
        sqlite3_bind_int(del, 1, version);
        sqlite3_step(del);
        exec_sql(db_, "COMMIT");
        log::info(std::format("Rolled back migration v{}", version));
        RefreshColumnInfo();
        return true;
    } catch (const std::exception& e) {
        exec_sql(db_, "ROLLBACK");
        log::error(std::format("Failed to rollback migration v{}: {}", version, e.what()));
        return false;
    }
}

// ── Column dictionary persistence ─────────────────────────────────────────────

std::vector<std::tuple<std::string, std::string, ValueId>> Database::GetColumnDictRows() const {
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

void Database::InsertColumnDictValue(const std::string& col, const std::string& value, ValueId id) {
    Statement stmt{db_, "INSERT INTO column_dictionary (column, value, value_id) VALUES (?, ?, ?)"};
    sqlite3_bind_text(stmt, 1, col.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);
    sqlite3_step(stmt);
}

// ── Health ────────────────────────────────────────────────────────────────────

bool Database::Ping() const {
    try {
        exec_sql(db_, "SELECT 1");
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace loglite
