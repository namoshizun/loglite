#ifndef LOGLITE_WRITER_DATABASE_HPP_
#define LOGLITE_WRITER_DATABASE_HPP_

#include "database.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace loglite {

class WriterDatabase final : public Database {
   public:
    explicit WriterDatabase(const Config& cfg);

    void Open();
    void Initialize();

    void CreateInternalTables();

    int Insert(const std::vector<nlohmann::json>& logs);
    int DeleteLogs(const std::vector<QueryFilter>& filters);

    void SetPragma(std::string_view name, std::string_view value);
    void IncrementalVacuum(int page_count);
    void Vacuum();
    void WALCheckpoint(std::string_view mode = "TRUNCATE");

    bool InsertActivityStats(const ActivityStatsRow& row);
    bool InsertDatabaseStats(const DatabaseStatsRow& row);
    int DeleteStatsBefore(std::string_view cutoff);

    std::vector<int> GetAppliedVersions() const;
    bool ApplyMigration(int version, const std::vector<std::string>& statements);
    bool RollbackMigration(int version, const std::vector<std::string>& statements);

    std::vector<std::tuple<std::string, std::string, ValueId>> GetColumnDictRows() const;
    bool InsertColumnDictValue(const std::string& col, const std::string& value, ValueId id);
};

}  // namespace loglite

#endif  // LOGLITE_WRITER_DATABASE_HPP_
