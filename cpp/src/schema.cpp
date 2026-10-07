#include "schema.hpp"

#include <ranges>
#include <stdexcept>

#include <fmt/format.h>

namespace loglite {

LogSchema LogSchema::From(const Config& cfg, std::span<const ColumnInfo> columns) {
    LogSchema schema;
    schema.timestamp_field_ = cfg.log_timestamp_field;
    schema.partitioned_ = cfg.partition_interval != PartitionInterval::kNone;

    const bool compress = cfg.compression.enabled;
    for (const auto& column : columns) {
        FieldSpec field;
        field.name = column.name;
        field.affinity = column.type;
        field.not_null = column.not_null;
        field.primary_key = column.is_pk;
        field.default_sql = column.default_sql;
        field.timestamp = column.name == cfg.log_timestamp_field;
        field.compressed = compress && cfg.compression.columns.end() !=
                                           std::ranges::find(cfg.compression.columns, column.name);
        // The server owns the integer primary key. Client-supplied ids are ignored.
        field.insertable = !column.is_pk;
        if (column.is_pk) schema.server_assigns_id_ = true;

        if (field.compressed && field.default_sql)
            throw std::runtime_error(fmt::format(
                "Compressed column '{}' declares a SQL default, which would bypass dictionary "
                "encoding",
                field.name));

        schema.fields_.push_back(std::move(field));
    }
    return schema;
}

const FieldSpec* LogSchema::Find(std::string_view name) const {
    const auto it = std::ranges::find(fields_, name, &FieldSpec::name);
    return it == fields_.end() ? nullptr : &*it;
}

std::vector<ColumnInfo> LogSchema::columns() const {
    std::vector<ColumnInfo> out;
    out.reserve(fields_.size());
    for (const auto& field : fields_) {
        out.push_back(ColumnInfo{.name = field.name,
                                 .type = field.affinity,
                                 .not_null = field.not_null,
                                 .is_pk = field.primary_key,
                                 .default_sql = field.default_sql});
    }
    return out;
}

}  // namespace loglite
