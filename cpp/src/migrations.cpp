#include "migrations.hpp"
#include "writer_database.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <iostream>
#include <ranges>
#include <stdexcept>

namespace loglite {

MigrationManager::MigrationManager(WriterDatabase& db, std::span<const Migration> migrations)
    : db_(db), migrations_(migrations.begin(), migrations.end()) {
    std::ranges::sort(migrations_, {}, &Migration::version);
}

bool MigrationManager::ApplyPendingMigrations(int start_version) {
    auto applied = db_.GetAppliedVersions();

    for (const auto& mg : migrations_) {
        if (mg.version <= start_version) continue;
        if (range_contains(applied, mg.version)) continue;

        bool ok = db_.ApplyMigration(mg.version, mg.rollout);
        // Mirror Python: apply ONE migration per call and return.
        return ok;
    }
    return false;
}

bool MigrationManager::RollbackMigration(int version, bool force) {
    auto it = std::ranges::find_if(migrations_,
                                   [version](const Migration& m) { return m.version == version; });
    if (it == migrations_.end())
        throw std::runtime_error(fmt::format("Migration v{} not found in config", version));

    if (!force) {
        std::cout << fmt::format("Roll back migration v{}? [y/N] ", version);
        std::string ans;
        std::getline(std::cin, ans);
        if (ans != "y" && ans != "Y") {
            log::info("Rollback cancelled.");
            return false;
        }
    }

    return db_.RollbackMigration(version, it->rollback);
}

}  // namespace loglite
