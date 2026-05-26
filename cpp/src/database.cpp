#include "database.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <algorithm>
#include <array>
#include <fmt/format.h>
#include <ranges>
#include <stdexcept>

namespace loglite {

namespace {

int normalize_vacuum_mode(std::string_view value) {
    if (value.size() == 1 && value[0] >= '0' && value[0] <= '2') return value[0] - '0';
    if (value == "NONE") return 0;
    if (value == "FULL") return 1;
    if (value == "INCREMENTAL") return 2;
    throw std::runtime_error(fmt::format("Unknown auto_vacuum value: '{}'", value));
}

}  // namespace

Statement::Statement(sqlite3* db, std::string_view sql) {
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &raw, nullptr) !=
        SQLITE_OK)
        throw std::runtime_error(fmt::format("sqlite3_prepare_v2: {}", sqlite3_errmsg(db)));
}

Database::Database(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog)
    : cfg_(cfg), catalog_(std::move(catalog)) {}

Database::~Database() { Close(); }

void Database::RefreshColumnInfo() {
    catalog_->log_column_info = FetchTableColumns(cfg_.log_table_name);
    catalog_->activity_stats_column_info = FetchTableColumns("activity_stats");
    catalog_->db_stats_column_info = FetchTableColumns("database_stats");
}

void Database::Close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::ensure_ok(int rc, std::string_view ctx) const {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
        throw std::runtime_error(fmt::format("{}: {}", ctx, sqlite3_errmsg(db_)));
}

void Database::exec_sql(std::string_view sql) const {
    char* errmsg{};
    int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error(fmt::format("sqlite3_exec: {}", msg));
    }
}

void Database::set_pragma(std::string_view name, std::string_view value) {
    exec_sql(fmt::format("PRAGMA {}={}", name, value));
}

void Database::apply_params(AccessMode mode) {
    constexpr auto kWriterOnlyPragmas =
        std::to_array<std::string_view>({"auto_vacuum", "journal_mode", "synchronous"});

    for (const auto& [k, v] : cfg_.sqlite_params) {
        if (mode == AccessMode::READ && range_contains(kWriterOnlyPragmas, k)) {
            // Read-only connection cannot set pragmas that require write access.
            continue;
        }

        if (mode == AccessMode::WRITE && k == "auto_vacuum") {
            auto current = GetPragma("auto_vacuum");
            if (normalize_vacuum_mode(current) != normalize_vacuum_mode(v)) {
                log::WARN(
                    "Changing auto_vacuum from {} to {} requires a full VACUUM. This may take a "
                    "while...",
                    current, v);
                set_pragma("auto_vacuum", v);
                exec_sql("VACUUM");
            }
        } else {
            set_pragma(k, v);
        }
    }
}

void Database::bind_param(sqlite3_stmt* stmt, int idx, const nlohmann::json& v) {
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

nlohmann::json Database::column_to_json(sqlite3_stmt* stmt, int col) {
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

nlohmann::json Database::serialize_value(const nlohmann::json& v) {
    if (v.is_null()) return nullptr;
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if (v.is_number()) return v;
    if (v.is_string()) return v;
    return v.dump();
}

std::vector<std::string> Database::pluck_column_names(const std::vector<ColumnInfo>& infos) {
    namespace rv = std::ranges::views;
    auto view = infos | rv::transform(&ColumnInfo::name);
    return std::vector<std::string>(std::ranges::begin(view), std::ranges::end(view));
}

std::vector<ColumnInfo> Database::FetchTableColumns(std::string_view table_name) const {
    std::vector<ColumnInfo> out;
    auto sql = fmt::format("PRAGMA table_info({})", table_name);
    Statement stmt{db_, sql};
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ColumnInfo ci;
        ci.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        ci.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        ci.not_null = sqlite3_column_int(stmt, 3) != 0;
        ci.is_pk = sqlite3_column_int(stmt, 5) != 0;
        out.push_back(std::move(ci));
    }
    return out;
}

std::string Database::GetPragma(std::string_view name) const {
    auto sql = fmt::format("PRAGMA {}", name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        return txt ? txt : "";
    }
    return "";
}

int64_t Database::GetMaxLogId() const {
    auto sql = fmt::format("SELECT MAX(id) FROM {}", cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        return sqlite3_column_int64(stmt, 0);
    return 0;
}

int64_t Database::GetMinLogId() const {
    auto sql = fmt::format("SELECT MIN(id) FROM {}", cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        return sqlite3_column_int64(stmt, 0);
    return 0;
}

std::string Database::GetMinTimestamp() const {
    auto sql = fmt::format("SELECT MIN({}) FROM {}", cfg_.log_timestamp_field, cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        return txt ? txt : "";
    }
    return "";
}

int64_t Database::GetSizeBytes() const {
    int64_t page_count = std::stoll(GetPragma("page_count"));
    int64_t page_size = std::stoll(GetPragma("page_size"));
    int64_t freelist = std::stoll(GetPragma("freelist_count"));
    return (page_count - freelist) * page_size;
}

double Database::GetSizeMB() const { return bytes_to_mb(GetSizeBytes()); }

const std::vector<ColumnInfo>& Database::GetColumnInfo() const { return catalog_->log_column_info; }

int64_t Database::EstimateLogRowCount() const {
    auto sql =
        fmt::format("SELECT COALESCE(MAX(id) - MIN(id) + 1, 0) FROM {}", cfg_.log_table_name);
    Statement stmt{db_, sql};
    if (sqlite3_step(stmt) == SQLITE_ROW) return sqlite3_column_int64(stmt, 0);
    return 0;
}

int64_t Database::EstimateAvgRowBytes() const {
    int64_t rowcnt = EstimateLogRowCount();
    int64_t db_size = GetSizeBytes();
    int64_t avg_row_bytes = db_size / std::max(int64_t{1}, rowcnt);
    if (avg_row_bytes == 0) avg_row_bytes = 1;
    return avg_row_bytes;
}

void Database::validate_field(std::string_view name) const {
    if (!std::ranges::any_of(catalog_->log_column_info,
                             [name](const ColumnInfo& ci) { return ci.name == name; }))
        throw std::runtime_error(fmt::format("Unknown field name: '{}'", name));
}

static constexpr std::string_view kAllowedOps[] = {"=", "!=", ">", ">=", "<", "<=", "~="};

Database::WhereClause Database::build_where_clause(const std::vector<QueryFilter>& filters) const {
    std::string sql_parts;
    std::vector<nlohmann::json> params;

    for (const auto& ft : filters) {
        validate_field(ft.field);
        if (!range_contains(kAllowedOps, ft.op))
            throw std::runtime_error(fmt::format("Unknown query operator: '{}'", ft.op));

        if (!sql_parts.empty()) sql_parts += " AND ";

        if (catalog_->compressed_columns.contains(ft.field)) {
            auto ids = catalog_->col_dict->QueryCandidates(ft);
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

}  // namespace loglite
