#include "reader_database.hpp"

#include "log.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <ranges>
#include <stdexcept>

namespace loglite {

namespace {

std::string JoinFields(const std::vector<std::string>& fields) {
    std::string joined;
    for (const auto& field : fields) {
        if (!joined.empty()) joined += ',';
        joined += field;
    }
    return joined;
}

}  // namespace

ReaderDatabase::ReaderDatabase(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog)
    : Database(cfg, std::move(catalog)) {}

void ReaderDatabase::Open() { Open(cfg_.db_path); }

void ReaderDatabase::Open(const std::filesystem::path& path) {
    ensure_ok(
        sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr),
        "sqlite3_open_v2");
    apply_params(AccessMode::READ);
    log::DEBUG("Opened reader SQLite connection: {}", path.string());
}

std::vector<std::string> ReaderDatabase::ResolveFields(
    const std::vector<std::string>& fields) const {
    std::vector<std::string> effective_fields;
    if (fields.size() == 1 && fields[0] == "*") {
        for (const auto& ci : catalog_->log_column_info) effective_fields.push_back(ci.name);
    } else {
        effective_fields.assign(fields.begin(), fields.end());
        for (const auto& f : effective_fields) validate_field(f);
    }
    return effective_fields;
}

nlohmann::json ReaderDatabase::DecodeRow(sqlite3_stmt* stmt,
                                         const std::vector<std::string>& fields) const {
    auto row = nlohmann::json::object();
    for (int column = 0; column < static_cast<int>(fields.size()); ++column) {
        const auto& field = fields[column];
        auto value = column_to_json(stmt, column);
        if (catalog_->compressed_columns.contains(field) && value.is_number_integer())
            value = catalog_->col_dict->GetValue(field, value.get<int>());
        row[field] = std::move(value);
    }
    return row;
}

int64_t ReaderDatabase::CountLogs(const std::vector<QueryFilter>& filters) const {
    auto [where, params] = build_where_clause(filters);
    Statement count{db_,
                    fmt::format("SELECT COUNT(*) FROM {} WHERE {}", cfg_.log_table_name, where)};
    for (int i = 0; i < static_cast<int>(params.size()); ++i) bind_param(count, i + 1, params[i]);
    return count.Step() == SQLITE_ROW ? sqlite3_column_int64(count, 0) : 0;
}

PaginatedQueryResult ReaderDatabase::Query(const std::vector<std::string>& fields,
                                           const std::vector<QueryFilter>& filters, int limit,
                                           int offset) const {
    const auto resolved = ResolveFields(fields);
    // Use a quick estimate of the total when no filters are applied.
    const int64_t total = filters.empty() ? EstimateLogRowCount() : CountLogs(filters);
    PaginatedQueryResult result{total, offset, limit, {}};
    if (total == 0) return result;
    for (auto& row : ReadLogs(resolved, filters, LogOrder::kNewestFirst, limit, offset))
        result.results.push_back(std::move(row.values));
    return result;
}

std::vector<ReaderDatabase::LogRow> ReaderDatabase::ReadLogs(
    const std::vector<std::string>& fields, const std::vector<QueryFilter>& filters, LogOrder order,
    int limit, int offset) const {
    auto [where, params] = build_where_clause(filters);
    const auto ordering = order == LogOrder::kIdAscending
                              ? std::string{"id ASC"}
                              : fmt::format("{} DESC, id DESC", cfg_.log_timestamp_field);
    Statement stmt{db_, fmt::format("SELECT {}, id FROM {} WHERE {} ORDER BY {} LIMIT ? OFFSET ?",
                                    JoinFields(fields), cfg_.log_table_name, where, ordering)};
    int parameter = 1;
    for (const auto& value : params) bind_param(stmt, parameter++, value);
    bind_param(stmt, parameter++, limit);
    bind_param(stmt, parameter, offset);

    // Cap the reservation so a huge `limit` cannot OOM before any row is read;
    // the vector grows if the result set is larger.
    std::vector<LogRow> rows;
    rows.reserve(static_cast<size_t>(std::clamp(limit, 0, 1024)));
    const auto id_column = static_cast<int>(fields.size());
    while (stmt.Step() == SQLITE_ROW)
        rows.push_back({sqlite3_column_int64(stmt, id_column), DecodeRow(stmt, fields)});
    return rows;
}

void ReaderDatabase::LoadReadDictionary() {
    if (catalog_->compressed_columns.empty()) return;
    LookupTable lookup;
    Statement stmt{db_, "SELECT column, value, value_id FROM column_dictionary"};
    while (stmt.Step() == SQLITE_ROW) {
        auto column = column_to_json(stmt, 0).get<std::string>();
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto text = value ? std::string{value, static_cast<size_t>(sqlite3_column_bytes(stmt, 1))}
                          : std::string{};
        lookup[column][std::move(text)] = sqlite3_column_int(stmt, 2);
    }
    catalog_->col_dict = std::make_shared<ColumnDictionary>(std::move(lookup), nullptr);
}

std::unique_ptr<ReaderDatabase> ReaderDatabase::OpenFile(const std::filesystem::path& path) const {
    auto local_catalog = std::make_shared<DatabaseCatalog>(cfg_);
    local_catalog->log_column_info = catalog_->log_column_info;
    auto reader = std::make_unique<ReaderDatabase>(cfg_, std::move(local_catalog));
    reader->Open(path);
    // Dictionary, count and rows use the same snapshot. Closing this short-lived
    // reader ends the read transaction, including on exceptions.
    reader->exec_sql("BEGIN");
    reader->LoadReadDictionary();
    return reader;
}

StatsQueryResult ReaderDatabase::QueryActivityStats(std::string_view since, std::string_view until,
                                                    const std::vector<std::string>& fields,
                                                    std::string_view ordering) const {
    const auto known = pluck_column_names(catalog_->activity_stats_column_info);
    const auto query_all_fields = fields.empty() || (fields.size() == 1 && fields[0] == "*");
    const auto resolved = query_all_fields ? known : fields;

    for (const auto& f : resolved)
        if (std::ranges::find(known, f) == known.end())
            throw std::runtime_error(fmt::format("Unknown activity_stats field: '{}'", f));

    std::string col_list;
    col_list.reserve(resolved.size() * 16);
    for (size_t i = 0; i < resolved.size(); ++i) {
        if (i) col_list += ", ";
        col_list += resolved[i];
    }

    std::string order = "DESC";
    if (ordering == "asc") order = "ASC";

    auto sql = fmt::format(
        "SELECT {} FROM activity_stats WHERE until >= ? AND until <= ? ORDER BY until {}", col_list,
        order);
    Statement stmt{db_, sql};
    sqlite3_bind_text(stmt, 1, since.data(), static_cast<int>(since.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, until.data(), static_cast<int>(until.size()), SQLITE_TRANSIENT);

    StatsQueryResult result;
    result.fields = resolved;
    while (stmt.Step() == SQLITE_ROW) {
        std::vector<nlohmann::json> row;
        row.reserve(resolved.size());
        for (int c = 0; c < static_cast<int>(resolved.size()); ++c) {
            row.push_back(column_to_json(stmt, c));
        }
        result.data.push_back(std::move(row));
    }
    return result;
}

StatsQueryResult ReaderDatabase::QueryDatabaseStats(std::string_view since, std::string_view until,
                                                    const std::vector<std::string>& fields,
                                                    std::string_view ordering) const {
    const auto known = pluck_column_names(catalog_->db_stats_column_info);
    const auto query_all_fields = fields.empty() || (fields.size() == 1 && fields[0] == "*");
    const auto resolved = query_all_fields ? known : fields;

    for (const auto& f : resolved)
        if (std::ranges::find(known, f) == known.end())
            throw std::runtime_error(fmt::format("Unknown database_stats field: '{}'", f));

    std::string col_list;
    col_list.reserve(resolved.size() * 16);
    for (size_t i = 0; i < resolved.size(); ++i) {
        if (i) col_list += ", ";
        col_list += resolved[i];
    }

    std::string order = "DESC";
    if (ordering == "asc") order = "ASC";

    auto sql = fmt::format(
        "SELECT {} FROM database_stats WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp "
        "{}",
        col_list, order);
    Statement stmt{db_, sql};
    sqlite3_bind_text(stmt, 1, since.data(), static_cast<int>(since.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, until.data(), static_cast<int>(until.size()), SQLITE_TRANSIENT);

    StatsQueryResult result;
    result.fields = resolved;
    while (stmt.Step() == SQLITE_ROW) {
        std::vector<nlohmann::json> row;
        row.reserve(resolved.size());
        for (int c = 0; c < static_cast<int>(resolved.size()); ++c) {
            row.push_back(column_to_json(stmt, c));
        }
        result.data.push_back(std::move(row));
    }
    return result;
}

bool ReaderDatabase::Ping() const {
    try {
        exec_sql("SELECT 1");
        return true;
    } catch (...) {
        return false;
    }
}

// ── ReadDatabasePool ───────────────────────────────────────────────────────────

ReadDatabasePool::ReadDatabasePool(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog,
                                   size_t size) {
    readers_.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        auto db = std::make_unique<ReaderDatabase>(cfg, catalog);
        db->Open();
        available_.push(db.get());
        readers_.push_back(std::move(db));
    }
}

ReadDatabasePool::~ReadDatabasePool() { Close(); }

void ReadDatabasePool::Close() {
    std::lock_guard lock(mtx_);
    if (closed_) return;
    closed_ = true;
    while (!available_.empty()) available_.pop();
    for (auto& db : readers_) db->Close();
    readers_.clear();
    cv_.notify_all();
}

ReaderDatabase& ReadDatabasePool::acquire() {
    std::unique_lock lock(mtx_);
    cv_.wait(lock, [this] { return closed_ || !available_.empty(); });

    if (closed_) throw std::runtime_error("read database pool is closed");
    ReaderDatabase* db = available_.front();
    available_.pop();
    return *db;
}

void ReadDatabasePool::release(ReaderDatabase& db) {
    std::lock_guard lock(mtx_);
    if (closed_) return;

    available_.push(&db);
    cv_.notify_one();
}

}  // namespace loglite
