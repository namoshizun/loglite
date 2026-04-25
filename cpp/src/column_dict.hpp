#pragma once

#include "types.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace loglite {

// Forward-declare so column_dict.hpp doesn't pull in the full database header.
class Database;

// ── In-memory enumeration dictionary ─────────────────────────────────────────
//
// Mirrors Python's ColumnDictionary.  For each compressed column, values are
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
        std::function<void(const std::string& col, const std::string& value, ValueId id)>;

    explicit ColumnDictionary(LookupTable lookup, PersistFn persist);

    // Return the id for (col, value), creating a new entry if needed.
    // Triggers an async DB persist for new entries via the PersistFn.
    ValueId get_or_create(const std::string& col, const std::string& value);

    // Reverse lookup: id → original value string.
    std::string get_value(const std::string& col, ValueId id) const;

    // For a filter on a compressed column, return the matching integer ids.
    // Used to convert a filter into an IN(?) clause.
    std::vector<ValueId> query_candidates(const QueryFilter& filter) const;

    const LookupTable& lookup() const { return lookup_; }

   private:
    LookupTable lookup_;
    PersistFn persist_;
};

}  // namespace loglite
