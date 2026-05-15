#include "column_dict.hpp"

#include <algorithm>
#include <format>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>

namespace loglite {

ColumnDictionary::ColumnDictionary(LookupTable lookup, PersistFn persist)
    : lookup_(std::move(lookup)), persist_(std::move(persist)) {}

ValueId ColumnDictionary::GetOrCreate(const std::string& col, const std::string& value) {
    // Writes are serialised by the strand, so only reader-writer contention exists.
    std::unique_lock wl(mtx_);

    auto& col_map = lookup_[col];
    if (auto it = col_map.find(value); it != col_map.end()) return it->second;

    // New value: assign next sequential id within this column.
    ValueId new_id = 1;
    if (!col_map.empty()) {
        auto max_it =
            std::ranges::max_element(col_map, {}, [](const auto& kv) { return kv.second; });
        new_id = max_it->second + 1;
    }

    if (persist_) {
        if (!persist_(col, value, new_id)) {
            throw std::runtime_error(
                std::format("Failed to update the compression table for column '{}' and value '{}'",
                            col, value));
        }
    }

    col_map[value] = new_id;
    return new_id;
}

std::string ColumnDictionary::GetValue(const std::string& col, ValueId id) const {
    std::shared_lock rl(mtx_);

    auto col_it = lookup_.find(col);
    if (col_it == lookup_.end())
        throw std::runtime_error(std::format("Unknown compressed column: '{}'", col));

    for (const auto& [v, vid] : col_it->second) {
        if (vid == id) return v;
    }
    throw std::runtime_error(std::format("No value for id={} in column '{}'", id, col));
}

std::vector<ValueId> ColumnDictionary::QueryCandidates(const QueryFilter& filter) const {
    std::shared_lock rl(mtx_);

    auto col_it = lookup_.find(filter.field);
    if (col_it == lookup_.end()) return {};

    const auto& col_map = col_it->second;
    std::string fval =
        filter.value.is_string() ? filter.value.get<std::string>() : filter.value.dump();

    const auto value_matches = [](std::string_view op, const std::string& fval_arg,
                                  const std::string& v) -> bool {
        if (op == "~=")
            return v.find(fval_arg) != std::string::npos || fval_arg.find(v) != std::string::npos;
        if (op == "=") return v == fval_arg;
        if (op == "!=") return v != fval_arg;
        if (op == ">") return v > fval_arg;
        if (op == ">=") return v >= fval_arg;
        if (op == "<") return v < fval_arg;
        if (op == "<=") return v <= fval_arg;
        return false;
    };

    namespace rv = std::ranges::views;
    auto matched =
        col_map |
        rv::filter([&](const auto& kv) { return value_matches(filter.op, fval, kv.first); }) |
        rv::transform([](const auto& kv) { return kv.second; });
    return std::vector<ValueId>(std::ranges::begin(matched), std::ranges::end(matched));
}

LookupTable ColumnDictionary::GetLookUp() const {
    std::shared_lock rl(mtx_);
    return lookup_;
}

}  // namespace loglite
