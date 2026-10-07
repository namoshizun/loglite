#ifndef LOGLITE_QUERY_PLAN_HPP_
#define LOGLITE_QUERY_PLAN_HPP_

#include "schema.hpp"
#include "types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace loglite {

enum class CmpOp { kEq, kNe, kLt, kLe, kGt, kGe, kContains };

struct Predicate {
    std::string field;
    CmpOp op{CmpOp::kEq};
    nlohmann::json operand;
};

// What transport is allowed to build. No dictionary ids, no SQL fragments.
struct QueryRequest {
    std::vector<std::string> fields;
    std::vector<Predicate> predicates;
    int limit{};
    int offset{};
};

// Validated logical plan. Physical encoding stays inside each file.
struct QueryPlan {
    std::vector<std::string> fields;
    std::vector<Predicate> predicates;
    int limit{};
    int offset{};
};

struct StoredPredicate {
    bool impossible{false};
    QueryFilter filter;
};

[[nodiscard]] std::optional<CmpOp> ParseCmpOp(std::string_view op);
[[nodiscard]] std::string_view ToQueryOperator(CmpOp op);

[[nodiscard]] QueryPlan PrepareQuery(const LogSchema& schema, const QueryRequest& request);

// File-local translation. Partitioned timestamps keep comparison meaning when the
// query instant is finer than the stored millisecond grid.
[[nodiscard]] StoredPredicate TranslatePredicate(const Predicate& predicate,
                                                 const LogSchema& schema);

[[nodiscard]] std::vector<QueryFilter> FiltersFrom(const std::vector<Predicate>& predicates);

}  // namespace loglite

#endif  // LOGLITE_QUERY_PLAN_HPP_
