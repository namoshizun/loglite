#ifndef LOGLITE_READER_DATABASE_HPP_
#define LOGLITE_READER_DATABASE_HPP_

#include "database.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace loglite {

class ReaderDatabase final : public Database {
   public:
    ReaderDatabase(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog);

    void Open();
    void Open(const std::filesystem::path& path);

    // A short-lived connection to another file with this schema, decoding through
    // that file's own column dictionary.
    [[nodiscard]] std::unique_ptr<ReaderDatabase> OpenFile(const std::filesystem::path& path) const;

    PaginatedQueryResult Query(const std::vector<std::string>& fields,
                               const std::vector<QueryFilter>& filters, int limit,
                               int offset) const;

    // Expands "*" and validates the projection.
    [[nodiscard]] std::vector<std::string> ResolveFields(
        const std::vector<std::string>& fields) const;
    [[nodiscard]] int64_t CountLogs(const std::vector<QueryFilter>& filters) const;
    void BindSnapshot(const std::vector<std::string>& fields,
                      const std::vector<QueryFilter>& filters) const {
        LoadSnapshot(fields, filters);
    }

    StatsQueryResult QueryActivityStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const;
    StatsQueryResult QueryDatabaseStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const;

    bool Ping() const;

   private:
    void LoadReadDictionary();
    void LoadSnapshot(const std::vector<std::string>& fields,
                      const std::vector<QueryFilter>& filters) const;
    StatsQueryResult QueryStatsTable(std::string_view table, std::string_view time_column,
                                     std::string_view since, std::string_view until,
                                     const std::vector<ColumnInfo>& schema,
                                     const std::vector<std::string>& fields,
                                     std::string_view ordering) const;
};

}  // namespace loglite

#endif  // LOGLITE_READER_DATABASE_HPP_
