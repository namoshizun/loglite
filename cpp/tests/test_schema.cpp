#include <gtest/gtest.h>

#include "handlers/schema.hpp"

using namespace loglite::handlers;

TEST(SchemaKindTest, NormalizesTypeAliasesAndCompressedStorage) {
    struct Case {
        const char* type;
        bool compressed;
        const char* kind;
    };
    const Case cases[]{
        {"INTEGER", false, "integer"},
        {"int", false, "integer"},
        {"REAL", false, "number"},
        {"FLOAT", false, "number"},
        {"NUMERIC(10,2)", false, "number"},
        {"TEXT", false, "text"},
        {"VARCHAR(64)", false, "text"},
        {"DATETIME", false, "datetime"},
        {"JSON", false, "json"},
        {"BLOB", false, "blob"},
        {"BOOLEAN", false, "boolean"},
        {"INTEGER", true, "text"},
        {"WEIRD", false, "text"},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.type);
        EXPECT_EQ(NormalizeColumnKind(c.type, c.compressed), c.kind);
    }
}
