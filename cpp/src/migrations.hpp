#ifndef LOGLITE_MIGRATIONS_HPP_
#define LOGLITE_MIGRATIONS_HPP_

#include "types.hpp"

#include <vector>

namespace loglite {

class WriterDatabase;

class MigrationManager {
   public:
    MigrationManager(WriterDatabase& db, std::span<const Migration> migrations);

    // Apply the first unapplied migration with version > start_version.
    // Mirrors Python's behaviour: one migration per call.
    // Call repeatedly (e.g. at startup) until all are applied.
    // Returns true if any migration was applied.
    bool ApplyPendingMigrations(int start_version = -1);

    // Rollback a specific version.  Prompts confirmation unless force=true.
    bool RollbackMigration(int version, bool force = false);

   private:
    WriterDatabase& db_;
    std::vector<Migration> migrations_;  // sorted by version asc
};

}  // namespace loglite

#endif  // LOGLITE_MIGRATIONS_HPP_
