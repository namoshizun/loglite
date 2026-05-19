#ifndef LOGLITE_DATABASE_CATALOG_HPP_
#define LOGLITE_DATABASE_CATALOG_HPP_

#include "column_dict.hpp"
#include "config.hpp"
#include "types.hpp"

#include <memory>
#include <set>
#include <vector>

namespace loglite {

// Shared schema + column dictionary across writer and read-pool connections.
// Populated by the writer during Initialize(); immutable schema for server lifetime.
struct DatabaseCatalog {
    explicit DatabaseCatalog(const Config& cfg);

    const Config& cfg;
    std::set<std::string> compressed_columns;
    std::vector<ColumnInfo> log_column_info;
    std::vector<ColumnInfo> activity_stats_column_info;
    std::vector<ColumnInfo> db_stats_column_info;
    std::shared_ptr<ColumnDictionary> col_dict;
};

}  // namespace loglite

#endif  // LOGLITE_DATABASE_CATALOG_HPP_
