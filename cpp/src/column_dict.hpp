#ifndef LOGLITE_COLUMN_DICT_HPP_
#define LOGLITE_COLUMN_DICT_HPP_

#include "types.hpp"

#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace loglite {

class Database;

// ── In-memory enumeration dictionary ─────────────────────────────────────────
//
// For each compressed column, values are
// stored in the DB as integer IDs and the actual string lives here.  This
// keeps the on-disk representation compact for low-cardinality columns (e.g.
// log level, service name).

using ValueId = int;
using ColumnName = std::string;
// column → { value_string → value_id }
using LookupTable = std::unordered_map<ColumnName, std::unordered_map<std::string, ValueId>>;

class ColumnDictionary {
   public:
    // Callback used to persist a new (column, value, id) entry to the DB.
    // Called asynchronously (fire-and-forget) so it must be thread-safe.
    using PersistFn =
        std::function<bool(const std::string& col, const std::string& value, ValueId id)>;

    explicit ColumnDictionary(LookupTable lookup, PersistFn persist);

    // Return the id for (col, value), creating a new entry if needed.
    // Triggers an async DB persist for new entries via the PersistFn.
    ValueId GetOrCreate(const std::string& col, const std::string& value);

    // Reverse lookup: id → original value string.
    std::string GetValue(const std::string& col, ValueId id) const;

    // For a filter on a compressed column, return the matching integer ids.
    // Used to convert a filter into an IN(?) clause.
    std::vector<ValueId> QueryCandidates(const QueryFilter& filter) const;

    // Returns a snapshot copy of the lookup table (thread-safe).
    LookupTable GetLookUp() const;

   private:
    mutable std::shared_mutex mtx_;
    LookupTable lookup_;
    PersistFn persist_;
};

}  // namespace loglite

#endif  // LOGLITE_COLUMN_DICT_HPP_
