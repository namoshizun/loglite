#ifndef LOGLITE_MIGRATIONS_HPP_
#define LOGLITE_MIGRATIONS_HPP_

#include "types.hpp"

#include <vector>

namespace loglite {

class Database;

class MigrationManager {
   public:
    MigrationManager(Database& db, std::span<const Migration> migrations);

    // Apply the first unapplied migration with version > start_version.
    // Mirrors Python's behaviour: one migration per call.
    // Call repeatedly (e.g. at startup) until all are applied.
    // Returns true if any migration was applied.
    bool apply_pending_migrations(int start_version = -1);

    // Rollback a specific version.  Prompts confirmation unless force=true.
    bool rollback_migration(int version, bool force = false);

   private:
    Database& db_;
    std::vector<Migration> migrations_;  // sorted by version asc
};

}  // namespace loglite

#endif  // LOGLITE_MIGRATIONS_HPP_
