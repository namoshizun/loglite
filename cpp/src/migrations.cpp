#include "migrations.hpp"
#include "database.hpp"
#include "log.hpp"

#include <algorithm>
#include <format>
#include <iostream>
#include <ranges>
#include <stdexcept>

namespace loglite {

MigrationManager::MigrationManager(Database& db, std::span<const Migration> migrations)
    : db_(db), migrations_(migrations.begin(), migrations.end()) {
    std::ranges::sort(migrations_, {}, &Migration::version);
}

bool MigrationManager::apply_pending_migrations(int start_version) {
    auto applied = db_.get_applied_versions();

    for (const auto& mg : migrations_) {
        if (mg.version <= start_version) continue;
        if (std::ranges::contains(applied, mg.version)) continue;

        bool ok = db_.apply_migration(mg.version, mg.rollout);
        // Mirror Python: apply ONE migration per call and return.
        return ok;
    }
    return false;
}

bool MigrationManager::rollback_migration(int version, bool force) {
    auto it = std::ranges::find_if(migrations_,
                                   [version](const Migration& m) { return m.version == version; });
    if (it == migrations_.end())
        throw std::runtime_error(std::format("Migration v{} not found in config", version));

    if (!force) {
        std::print("Roll back migration v{}? [y/N] ", version);
        std::string ans;
        std::getline(std::cin, ans);
        if (ans != "y" && ans != "Y") {
            log::info("Rollback cancelled.");
            return false;
        }
    }

    return db_.rollback_migration(version, it->rollback);
}

}  // namespace loglite
