#include "query_plan.hpp"

#include "utils.hpp"

#include <stdexcept>

#include <fmt/format.h>

namespace loglite {

namespace {

using TimePoint = std::chrono::system_clock::time_point;

TimePoint FloorMs(TimePoint instant) {
    return std::chrono::floor<std::chrono::milliseconds>(instant);
}

bool OnMillisecondGrid(TimePoint instant) { return FloorMs(instant) == instant; }

std::string StoredText(TimePoint instant) { return format_utc(FloorMs(instant)); }

}  // namespace

std::optional<CmpOp> ParseCmpOp(std::string_view op) {
    if (op == "=") return CmpOp::kEq;
    if (op == "!=") return CmpOp::kNe;
    if (op == "<") return CmpOp::kLt;
    if (op == "<=") return CmpOp::kLe;
    if (op == ">") return CmpOp::kGt;
    if (op == ">=") return CmpOp::kGe;
    if (op == "~=") return CmpOp::kContains;
    return std::nullopt;
}

std::string_view ToQueryOperator(CmpOp op) {
    switch (op) {
    case CmpOp::kEq:
        return "=";
    case CmpOp::kNe:
        return "!=";
    case CmpOp::kLt:
        return "<";
    case CmpOp::kLe:
        return "<=";
    case CmpOp::kGt:
        return ">";
    case CmpOp::kGe:
        return ">=";
    case CmpOp::kContains:
        return "~=";
    }
    return "=";
}

QueryPlan PrepareQuery(const LogSchema& schema, const QueryRequest& request) {
    QueryPlan plan;
    plan.limit = request.limit;
    plan.offset = request.offset;

    if (request.fields.size() == 1 && request.fields.front() == "*") {
        plan.fields.reserve(schema.fields().size());
        for (const auto& field : schema.fields()) plan.fields.push_back(field.name);
    } else {
        for (const auto& field : request.fields) {
            if (field.empty() || field.front() == ' ' || !schema.Find(field))
                throw std::runtime_error(fmt::format("Unknown field name: '{}'", field));
            plan.fields.push_back(field);
        }
    }

    plan.predicates.reserve(request.predicates.size());
    for (const auto& predicate : request.predicates) {
        if (!schema.Find(predicate.field))
            throw std::runtime_error(fmt::format("Unknown field name: '{}'", predicate.field));
        plan.predicates.push_back(predicate);
    }
    return plan;
}

StoredPredicate TranslatePredicate(const Predicate& predicate, const LogSchema& schema) {
    StoredPredicate stored;
    stored.filter = {predicate.field, std::string{ToQueryOperator(predicate.op)},
                     predicate.operand};

    const auto* field = schema.Find(predicate.field);
    if (!field || !field->timestamp || !schema.partitioned() || !predicate.operand.is_string())
        return stored;

    const auto instant = parse_iso8601(predicate.operand.get_ref<const std::string&>());
    if (!instant) return stored;

    const bool aligned = OnMillisecondGrid(*instant);
    const auto op = predicate.op;

    if (op == CmpOp::kEq) {
        if (!aligned)
            stored.impossible = true;
        else
            stored.filter.value = StoredText(*instant);
        return stored;
    }
    if (op == CmpOp::kNe) {
        if (!aligned) {
            stored.filter.op = "=";
            stored.filter.value = "1";
            stored.filter.field = "1";
            stored.impossible = true;
        } else {
            stored.filter.value = StoredText(*instant);
        }
        return stored;
    }
    if (op == CmpOp::kContains) return stored;

    // Stored instants sit on the millisecond grid. Shift the bound so `<`, `>=`
    // and equality keep their meaning when the query carries extra precision.
    TimePoint bound = *instant;
    CmpOp rewritten = op;
    if (!aligned) {
        if (op == CmpOp::kLt || op == CmpOp::kLe) {
            bound = FloorMs(*instant);
            rewritten = CmpOp::kLe;
        } else {
            bound = FloorMs(*instant) + std::chrono::milliseconds{1};
            rewritten = CmpOp::kGe;
        }
    }
    stored.filter.op = std::string{ToQueryOperator(rewritten)};
    stored.filter.value = format_utc(bound);
    return stored;
}

std::vector<QueryFilter> FiltersFrom(const std::vector<Predicate>& predicates) {
    std::vector<QueryFilter> filters;
    filters.reserve(predicates.size());
    for (const auto& predicate : predicates)
        filters.push_back(
            {predicate.field, std::string{ToQueryOperator(predicate.op)}, predicate.operand});
    return filters;
}

}  // namespace loglite
