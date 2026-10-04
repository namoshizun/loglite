#ifndef LOGLITE_WRITER_DATABASE_HPP_
#define LOGLITE_WRITER_DATABASE_HPP_

#include "database.hpp"

#include <boost/asio.hpp>

#include <concepts>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace asio = boost::asio;

namespace loglite {

class WriterDatabase final : public Database {
   public:
    explicit WriterDatabase(const Config& cfg);

    void Open();
    void Open(const std::filesystem::path& path);
    // Applies the configured migrations when auto_rollout is set.
    void Initialize();
    // Prepares one file: internal tables, the given migrations, schema and dictionary.
    void Initialize(std::span<const Migration> migrations);

    void CreateInternalTables();

    int Insert(const std::vector<nlohmann::json>& logs) { return InsertRows(logs); }
    // A positive first_id assigns consecutive IDs; otherwise SQLite assigns them.
    int InsertRows(std::span<const nlohmann::json> logs, int64_t first_id = 0);
    int DeleteLogs(const std::vector<QueryFilter>& filters);
    int DeleteOldLogs(std::string_view cutoff, int64_t limit);
    int DeleteOldestLogs(int64_t count);
    [[nodiscard]] int64_t GetCommittedLogId() const noexcept { return committed_id_; }

    void SetPragma(std::string_view name, std::string_view value);
    void IncrementalVacuum(int page_count);
    void Vacuum();
    void WALCheckpoint(std::string_view mode = "TRUNCATE");

    bool InsertActivityStats(const ActivityStatsRow& row);
    bool InsertDatabaseStats(const DatabaseStatsRow& row);
    int DeleteStatsBefore(std::string_view cutoff);

    // Partition metadata, kept in the control database (logs.db).
    [[nodiscard]] std::optional<std::string> GetPartitionInterval() const;
    void InitPartitionState(std::string_view interval);
    int64_t ReserveLogIds(int64_t count);  // returns the first reserved ID
    void ObserveLogId(int64_t id);

    std::vector<int> GetAppliedVersions() const;
    bool ApplyMigration(int version, const std::vector<std::string>& statements);
    bool RollbackMigration(int version, const std::vector<std::string>& statements);

    std::vector<std::tuple<std::string, std::string, ValueId>> GetColumnDictRows() const;
    bool InsertColumnDictValue(const std::string& col, const std::string& value, ValueId id);

    template <std::invocable<WriterDatabase&> F>
    asio::awaitable<std::invoke_result_t<F, WriterDatabase&>> AsyncUseConnection(
        asio::any_io_executor write_strand_ex, F&& f) {
        using Result = std::invoke_result_t<F, WriterDatabase&>;
        return asio::co_spawn(
            std::move(write_strand_ex),
            [this, f = std::forward<F>(f)]() mutable -> asio::awaitable<Result> {
                co_return std::invoke(std::move(f), *this);
            },
            asio::use_awaitable);
    }

   private:
    void LoadColumnDictionary();

    int64_t committed_id_{};
};

}  // namespace loglite

#endif  // LOGLITE_WRITER_DATABASE_HPP_
