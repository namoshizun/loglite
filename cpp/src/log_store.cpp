#include "log_store.hpp"

#include "log.hpp"
#include "migrations.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

namespace loglite {

namespace {

bool SameSchema(const std::vector<ColumnInfo>& lhs, const std::vector<ColumnInfo>& rhs) {
    return std::ranges::equal(lhs, rhs, [](const ColumnInfo& a, const ColumnInfo& b) {
        return a.name == b.name && a.type == b.type && a.not_null == b.not_null &&
               a.is_pk == b.is_pk;
    });
}

}  // namespace

void LogStore::Open() {
    cfg_.validate();
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(cfg_.sqlite_dir)) {
        if (!entry.is_regular_file()) continue;
        const auto interval = PartitionScheme::IntervalOf(entry.path().filename().string());
        if (!interval) continue;
        if (*interval != cfg_.partition_interval)
            throw std::runtime_error(
                "Existing partition files use a different partition_interval; keep the original "
                "interval or select a new sqlite_dir.");
        files.push_back(entry.path());
    }
    if (!files.empty() && !std::filesystem::exists(cfg_.db_path))
        throw std::runtime_error(
            "Partition files exist without control logs.db; restore logs.db before starting "
            "LogLite.");

    control_.Open();
    control_.CreateInternalTables();
    const auto persisted = control_.GetPartitionInterval();
    if (!scheme_.partitioned()) {
        if (persisted)
            throw std::runtime_error(
                "Cannot change the persisted partition_interval; keep the original interval or "
                "select a new sqlite_dir.");
        registry_.Add(scheme_.Route({}));
        return;
    }

    if (!persisted) {
        if (!files.empty())
            throw std::runtime_error(
                "Partition files exist without control ID metadata; restore the original logs.db "
                "before starting LogLite.");
        if (!control_.FetchTableColumns(cfg_.log_table_name).empty() && control_.CountLogRows() > 0)
            throw std::runtime_error(
                "Time partitioning (LogLite 1.4.0) is not backwards compatible with an existing "
                "database that holds logs; select a new sqlite_dir.");
        control_.InitPartitionState(ToString(cfg_.partition_interval));
    } else if (*persisted != ToString(cfg_.partition_interval)) {
        throw std::runtime_error(
            "Cannot change the persisted partition_interval; keep the original interval or "
            "select a new sqlite_dir.");
    }
    // The ID reservation must be durable before a different file commits.
    control_.SetPragma("synchronous", "FULL");
    for (const auto& path : files) registry_.Add(*scheme_.Parse(path));
}

void LogStore::Initialize() {
    if (cfg_.auto_rollout)
        while (Rollout()) {
        }
    control_.Initialize(std::span<const Migration>{});
    int64_t max_id = control_.GetCommittedLogId();
    if (!scheme_.partitioned()) {
        const int64_t rows = control_.GetColumnInfo().empty() ? 0 : control_.EstimateLogRowCount();
        registry_.Update(cfg_.db_path, max_id, rows);
    } else {
        ValidateSchema();
        const auto versions = control_.GetAppliedVersions();
        VisitPartitionFiles([&](WriterDatabase& file, const Partition& partition) {
            if (file.GetAppliedVersions() != versions)
                throw std::runtime_error(
                    "Partition migration versions differ from logs.db; finish rollout or rollback "
                    "before starting LogLite.");
            file.RefreshColumnInfo();
            if (!SameSchema(file.GetColumnInfo(), control_.GetColumnInfo()))
                throw std::runtime_error("Partition log schema differs from logs.db");
            const int64_t file_max = file.GetMaxLogId();
            registry_.Update(partition.path, file_max, file.CountLogRows());
            max_id = std::max(max_id, file_max);
        });
        control_.ObserveLogId(max_id);
    }
    committed_id_.store(max_id, std::memory_order_release);
}

void LogStore::Close() {
    std::unique_lock lease{lease_};
    writer_.reset();
    control_.Close();
}

void LogStore::ValidateSchema() const {
    const auto& columns = control_.GetColumnInfo();
    const auto id = std::ranges::find(columns, "id", &ColumnInfo::name);
    std::string id_type = id == columns.end() ? "" : id->type;
    std::ranges::transform(id_type, id_type.begin(),
                           [](unsigned char c) { return std::toupper(c); });
    if (id == columns.end() || !id->is_pk || id_type != "INTEGER" ||
        std::ranges::count_if(columns, &ColumnInfo::is_pk) != 1)
        throw std::runtime_error(
            "Time partitioning requires the log table to have id INTEGER PRIMARY KEY.");
    if (std::ranges::find(columns, cfg_.log_timestamp_field, &ColumnInfo::name) == columns.end())
        throw std::runtime_error("Time partitioning requires the configured timestamp field");
}

// New partitions replay exactly the history applied to logs.db.
std::vector<Migration> LogStore::ApprovedMigrations() const {
    std::vector<Migration> approved;
    for (const int version : control_.GetAppliedVersions()) {
        const auto migration = std::ranges::find(cfg_.migrations, version, &Migration::version);
        if (migration == cfg_.migrations.end())
            throw std::runtime_error(fmt::format(
                "Config is missing migration v{}, which new partitions must replay", version));
        approved.push_back(*migration);
    }
    return approved;
}

bool LogStore::Rollout(int start_version) {
    writer_.reset();
    bool changed = false;
    // Repair files left behind by an interrupted command before advancing logs.db.
    if (scheme_.partitioned()) {
        const auto approved = ApprovedMigrations();
        VisitPartitionFiles([&](WriterDatabase& file, const Partition&) {
            MigrationManager manager{file, approved};
            while (manager.ApplyPendingMigrations()) changed = true;
        });
    }

    const auto applied = control_.GetAppliedVersions();
    const Migration* pending = nullptr;
    for (const auto& migration : cfg_.migrations) {
        if (migration.version <= start_version || range_contains(applied, migration.version))
            continue;
        if (!pending || migration.version < pending->version) pending = &migration;
    }
    if (!pending) return changed;

    // Pass only this migration: a file already repaired during a previous attempt
    // must not advance to the following version before the control file does.
    const auto selected = std::span{pending, 1};
    VisitPartitionFiles([&](WriterDatabase& file, const Partition&) {
        MigrationManager{file, selected}.ApplyPendingMigrations(start_version);
    });
    return MigrationManager{control_, selected}.ApplyPendingMigrations(start_version);
}

bool LogStore::Rollback(int version, bool force) {
    const auto migration = std::ranges::find(cfg_.migrations, version, &Migration::version);
    if (migration == cfg_.migrations.end())
        throw std::runtime_error(fmt::format("Migration v{} not found in config", version));
    if (!force && !MigrationManager::ConfirmRollback(version)) return false;

    writer_.reset();
    bool rolled_back = false;
    VisitPartitionFiles([&](WriterDatabase& file, const Partition&) {
        rolled_back |= file.RollbackMigration(version, migration->rollback);
    });
    rolled_back |= control_.RollbackMigration(version, migration->rollback);
    return rolled_back;
}

WriterDatabase& LogStore::Writer(const Partition& partition) {
    if (partition.path == cfg_.db_path) return control_;
    if (writer_ && writer_path_ == partition.path) return *writer_;
    writer_.reset();
    auto file = std::make_unique<WriterDatabase>(cfg_);
    file->Open(partition.path);
    file->Initialize(ApprovedMigrations());
    if (file->GetAppliedVersions() != control_.GetAppliedVersions())
        throw std::runtime_error("Partition migration versions differ from logs.db");
    registry_.Add(partition);
    writer_path_ = partition.path;
    writer_ = std::move(file);
    return *writer_;
}

int LogStore::Insert(std::vector<nlohmann::json>& logs, const Acknowledge& acknowledge) {
    const auto ingestion = std::chrono::system_clock::now();
    std::vector<std::pair<Partition, nlohmann::json>> placed;
    placed.reserve(logs.size());
    for (auto& entry : logs) {
        auto partition = scheme_.Place(entry, ingestion);
        placed.emplace_back(std::move(partition), std::move(entry));
    }
    std::ranges::stable_sort(placed, {}, [](const auto& item) { return item.first.since; });
    for (size_t i = 0; i < logs.size(); ++i) logs[i] = std::move(placed[i].second);

    int inserted = 0;
    for (size_t begin = 0; begin < logs.size();) {
        const auto& partition = placed[begin].first;
        size_t end = begin + 1;
        while (end < logs.size() && placed[end].first.since == partition.since) ++end;

        auto& file = Writer(partition);
        const auto group = std::span<const nlohmann::json>{logs}.subspan(begin, end - begin);
        const int64_t first_id =
            scheme_.partitioned() ? control_.ReserveLogIds(static_cast<int64_t>(group.size())) : 0;
        const int count = file.InsertRows(group, first_id);
        registry_.Update(partition.path, file.GetCommittedLogId(), count);
        // Reserved IDs exceed every committed ID. Without reservations SQLite reuses
        // IDs after the newest rows are deleted, and the watermark must follow.
        if (count > 0) committed_id_.store(file.GetCommittedLogId(), std::memory_order_release);
        inserted += count;
        if (acknowledge) acknowledge(end);
        begin = end;
    }
    return inserted;
}

int64_t LogStore::EstimateLogRowCount() const {
    int64_t rows = 0;
    for (const auto& entry : registry_.Snapshot()) rows += entry.rows;
    return rows;
}

int64_t LogStore::GetSizeBytes() {
    int64_t bytes = control_.GetSizeBytes();
    VisitPartitionFiles(
        [&](WriterDatabase& file, const Partition&) { bytes += file.GetSizeBytes(); });
    return bytes;
}

}  // namespace loglite
