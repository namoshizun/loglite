#include "database_catalog.hpp"

namespace loglite {

DatabaseCatalog::DatabaseCatalog(const Config& cfg) : cfg(cfg) {
    if (cfg.compression.enabled) {
        for (const auto& c : cfg.compression.columns) compressed_columns.insert(c);
    }
}

}  // namespace loglite
