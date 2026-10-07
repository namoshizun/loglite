#ifndef LOGLITE_LOG_STORE_HPP_
#define LOGLITE_LOG_STORE_HPP_

#include "partition.hpp"
#include "writer_database.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace loglite {

// Owns logical storage: the control database (logs.db), the partition files and
// their lifecycle, global log IDs, migrations and retention. Each WriterDatabase
// or ReaderDatabase stays responsible for one file.
// Writes run on the server's write strand. The store outlives its reader pool.
class LogStore {
   public:
    using Acknowledge = std::function<void(size_t)>;
    using OnCommitted = std::function<void(const WriterDatabase&, int inserted)>;

    explicit LogStore(const Config& cfg) : cfg_(cfg), scheme_(cfg), control_(cfg) {}

    void Open();
    void Initialize();
    void Close();

    // Normalizes, routes and reorders `logs` in place so each file commits once.
    // After each file commits, `acknowledge` receives the committed prefix of `logs`.
    int Insert(std::vector<nlohmann::json>& logs, const Acknowledge& acknowledge = {},
               const OnCommitted& on_committed = {});
    int Insert(std::vector<nlohmann::json>&& logs) { return Insert(logs); }
    void Maintain();

    bool Rollout(int start_version = -1);
    bool Rollback(int version, bool force = false);

    [[nodiscard]] int64_t GetCommittedLogId() const noexcept {
        return committed_id_.load(std::memory_order_acquire);
    }
    [[nodiscard]] int64_t EstimateLogRowCount() const;
    [[nodiscard]] int64_t GetSizeBytes();
    [[nodiscard]] const std::vector<ColumnInfo>& GetColumnInfo() const {
        return control_.GetColumnInfo();
    }
    [[nodiscard]] std::shared_ptr<DatabaseCatalog> catalog() const { return control_.catalog(); }

    bool InsertActivityStats(const ActivityStatsRow& row) {
        return control_.InsertActivityStats(row);
    }
    bool InsertDatabaseStats(const DatabaseStatsRow& row) {
        return control_.InsertDatabaseStats(row);
    }
    int DeleteStatsBefore(std::string_view cutoff) { return control_.DeleteStatsBefore(cutoff); }

    // Read side. Partition files are only removed while no read lease is held.
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }
    [[nodiscard]] const PartitionScheme& scheme() const noexcept { return scheme_; }
    [[nodiscard]] std::vector<PartitionRegistry::Entry> Partitions() const {
        return registry_.Snapshot();
    }
    [[nodiscard]] std::shared_lock<std::shared_mutex> ReadLease() const {
        return std::shared_lock{lease_};
    }

    template <std::invocable<LogStore&> F>
    asio::awaitable<std::invoke_result_t<F, LogStore&>> AsyncUseConnection(
        asio::any_io_executor write_strand_ex, F&& f) {
        using Result = std::invoke_result_t<F, LogStore&>;
        return asio::co_spawn(
            std::move(write_strand_ex),
            [this, f = std::forward<F>(f)]() mutable -> asio::awaitable<Result> {
                co_return std::invoke(std::move(f), *this);
            },
            asio::use_awaitable);
    }

   private:
    // The control file reuses its connection; other files get a short-lived one,
    // so maintenance never holds many partition files open.
    template <typename F>
    void UseFile(const std::filesystem::path& path, F&& use) {
        if (path == cfg_.db_path) return use(control_);
        WriterDatabase file{cfg_};
        file.Open(path);
        use(file);
    }

    // Visits every partition file except logs.db; a no-op without partitioning.
    template <typename F>
    void VisitPartitionFiles(F&& visit) {
        for (const auto& entry : registry_.Snapshot())
            if (entry.partition.path != cfg_.db_path)
                UseFile(entry.partition.path,
                        [&](WriterDatabase& file) { visit(file, entry.partition); });
    }

    WriterDatabase& Writer(const Partition& partition);
    [[nodiscard]] std::vector<Migration> ApprovedMigrations() const;
    void ValidateSchema() const;
    void RemoveFiles(const std::vector<std::filesystem::path>& paths);

    const Config& cfg_;
    PartitionScheme scheme_;
    WriterDatabase control_;
    PartitionRegistry registry_;
    mutable std::shared_mutex lease_;
    std::unique_ptr<WriterDatabase> writer_;
    std::filesystem::path writer_path_;
    std::atomic<int64_t> committed_id_{};
};

}  // namespace loglite

#endif  // LOGLITE_LOG_STORE_HPP_
