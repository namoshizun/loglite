#include "column_dict.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

namespace loglite {

ColumnDictionary::ColumnDictionary(LookupTable lookup, PersistFn persist)
    : lookup_(std::move(lookup)), persist_(std::move(persist)) {}

ValueId ColumnDictionary::get_or_create(const std::string& col, const std::string& value) {
    auto& col_map = lookup_[col];

    if (auto it = col_map.find(value); it != col_map.end()) return it->second;

    // New value: assign next sequential id within this column.
    ValueId new_id = 1;
    if (!col_map.empty()) {
        auto max_it =
            std::ranges::max_element(col_map, {}, [](const auto& kv) { return kv.second; });
        new_id = max_it->second + 1;
    }

    col_map[value] = new_id;
    if (persist_) persist_(col, value, new_id);
    return new_id;
}

std::string ColumnDictionary::get_value(const std::string& col, ValueId id) const {
    auto col_it = lookup_.find(col);
    if (col_it == lookup_.end())
        throw std::runtime_error(std::format("Unknown compressed column: '{}'", col));

    for (const auto& [v, vid] : col_it->second) {
        if (vid == id) return v;
    }
    throw std::runtime_error(std::format("No value for id={} in column '{}'", id, col));
}

std::vector<ValueId> ColumnDictionary::query_candidates(const QueryFilter& filter) const {
    auto col_it = lookup_.find(filter.field);
    if (col_it == lookup_.end()) return {};

    const auto& col_map = col_it->second;
    const auto& op = filter.op;
    // Value from the filter as a string for comparison.
    std::string fval =
        filter.value.is_string() ? filter.value.get<std::string>() : filter.value.dump();

    std::vector<ValueId> ids;
    for (const auto& [v, id] : col_map) {
        bool match = false;
        if (op == "~=") {
            // Substring match (both directions, mirroring the Python implementation).
            match = v.find(fval) != std::string::npos || fval.find(v) != std::string::npos;
        } else if (op == "=") {
            match = v == fval;
        } else if (op == "!=") {
            match = v != fval;
        } else if (op == ">") {
            match = v > fval;
        } else if (op == ">=") {
            match = v >= fval;
        } else if (op == "<") {
            match = v < fval;
        } else if (op == "<=") {
            match = v <= fval;
        }

        if (match) ids.push_back(id);
    }
    return ids;
}

}  // namespace loglite
