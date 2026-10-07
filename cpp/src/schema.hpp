#ifndef LOGLITE_SCHEMA_HPP_
#define LOGLITE_SCHEMA_HPP_

#include "config.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace loglite {
struct Config;
}

namespace loglite {

// Immutable logical log schema for one process run. Distinct from each file's
// dictionary: this describes fields, defaults and compression, not encoded ids.
struct FieldSpec {
    std::string name;
    std::string affinity;
    bool not_null{false};
    bool primary_key{false};
    bool insertable{true};
    bool compressed{false};
    bool timestamp{false};
    std::optional<std::string> default_sql;
};

class LogSchema {
   public:
    LogSchema() = default;

    // Throws when a compressed column declares a SQL default. That default would
    // bypass dictionary encoding and break later filtering.
    [[nodiscard]] static LogSchema From(const Config& cfg, std::span<const ColumnInfo> columns);

    [[nodiscard]] bool usable() const noexcept { return !fields_.empty(); }
    [[nodiscard]] const std::vector<FieldSpec>& fields() const noexcept { return fields_; }
    [[nodiscard]] std::string_view timestamp_field() const noexcept { return timestamp_field_; }
    [[nodiscard]] bool partitioned() const noexcept { return partitioned_; }
    [[nodiscard]] bool server_assigns_id() const noexcept { return server_assigns_id_; }

    [[nodiscard]] const FieldSpec* Find(std::string_view name) const;
    [[nodiscard]] std::vector<ColumnInfo> columns() const;

   private:
    std::vector<FieldSpec> fields_;
    std::string timestamp_field_;
    bool partitioned_{false};
    bool server_assigns_id_{false};
};

}  // namespace loglite

#endif  // LOGLITE_SCHEMA_HPP_
