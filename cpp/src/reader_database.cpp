#include "reader_database.hpp"

#include "log.hpp"

#include <format>
#include <ranges>
#include <stdexcept>

namespace loglite {

ReaderDatabase::ReaderDatabase(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog)
    : Database(cfg, std::move(catalog)) {}

void ReaderDatabase::Open() {
    auto path = cfg_.db_path.string();
    ensure_ok(
        sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr),
        "sqlite3_open_v2");
    apply_params(AccessMode::READ);
    log::debug(std::format("Opened reader SQLite connection: {}", path));
}

PaginatedQueryResult ReaderDatabase::Query(const std::vector<std::string>& fields,
                                           const std::vector<QueryFilter>& filters, int limit,
                                           int offset) const {
    std::vector<std::string> effective_fields;
    if (fields.size() == 1 && fields[0] == "*") {
        for (const auto& ci : catalog_->log_column_info) effective_fields.push_back(ci.name);
    } else {
        effective_fields.assign(fields.begin(), fields.end());
        for (const auto& f : effective_fields) validate_field(f);
    }

    auto [where, params] = build_where_clause(filters);

    auto count_sql = std::format("SELECT COUNT(id) FROM {} WHERE {}", cfg_.log_table_name, where);
    Statement count_stmt{db_, count_sql};
    for (int i = 0; i < static_cast<int>(params.size()); ++i)
        bind_param(count_stmt, i + 1, params[i]);

    int total = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) total = sqlite3_column_int(count_stmt, 0);

    if (total == 0) return {total, offset, limit, {}};

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
            if (catalog_->compressed_columns.contains(fname) && val.is_number_integer()) {
                val = catalog_->col_dict->GetValue(fname, val.get<int>());
            }
            row[fname] = std::move(val);
        }
        results.push_back(std::move(row));
    }

    return {total, offset, limit, std::move(results)};
}

StatsQueryResult ReaderDatabase::QueryActivityStats(std::string_view since, std::string_view until,
                                                    const std::vector<std::string>& fields,
                                                    std::string_view ordering) const {
    const auto known = pluck_column_names(catalog_->activity_stats_column_info);
    const auto query_all_fields = fields.empty() || (fields.size() == 1 && fields[0] == "*");
    const auto resolved = query_all_fields ? known : fields;

    for (const auto& f : resolved)
        if (std::ranges::find(known, f) == known.end())
            throw std::runtime_error(std::format("Unknown activity_stats field: '{}'", f));

    std::string col_list;
    col_list.reserve(resolved.size() * 16);
    for (size_t i = 0; i < resolved.size(); ++i) {
        if (i) col_list += ", ";
        col_list += resolved[i];
    }

    std::string order = "DESC";
    if (ordering == "asc") order = "ASC";

    auto sql = std::format(
        "SELECT {} FROM activity_stats WHERE until >= ? AND until <= ? ORDER BY until {}", col_list,
        order);
    Statement stmt{db_, sql};
    sqlite3_bind_text(stmt, 1, since.data(), static_cast<int>(since.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, until.data(), static_cast<int>(until.size()), SQLITE_TRANSIENT);

    StatsQueryResult result;
    result.fields = resolved;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
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
            throw std::runtime_error(std::format("Unknown database_stats field: '{}'", f));

    std::string col_list;
    col_list.reserve(resolved.size() * 16);
    for (size_t i = 0; i < resolved.size(); ++i) {
        if (i) col_list += ", ";
        col_list += resolved[i];
    }

    std::string order = "DESC";
    if (ordering == "asc") order = "ASC";

    auto sql = std::format(
        "SELECT {} FROM database_stats WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp "
        "{}",
        col_list, order);
    Statement stmt{db_, sql};
    sqlite3_bind_text(stmt, 1, since.data(), static_cast<int>(since.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, until.data(), static_cast<int>(until.size()), SQLITE_TRANSIENT);

    StatsQueryResult result;
    result.fields = resolved;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
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
