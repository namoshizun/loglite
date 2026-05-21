#include <gtest/gtest.h>

#include "handlers/schema.hpp"

using namespace loglite::handlers;

TEST(SchemaKindTest, IntegerTypes) {
    EXPECT_EQ(NormalizeColumnKind("INTEGER", false), "integer");
    EXPECT_EQ(NormalizeColumnKind("int", false), "integer");
}

TEST(SchemaKindTest, NumberTypes) {
    EXPECT_EQ(NormalizeColumnKind("REAL", false), "number");
    EXPECT_EQ(NormalizeColumnKind("FLOAT", false), "number");
    EXPECT_EQ(NormalizeColumnKind("NUMERIC(10,2)", false), "number");
}

TEST(SchemaKindTest, TextAndDatetime) {
    EXPECT_EQ(NormalizeColumnKind("TEXT", false), "text");
    EXPECT_EQ(NormalizeColumnKind("VARCHAR(64)", false), "text");
    EXPECT_EQ(NormalizeColumnKind("DATETIME", false), "datetime");
    EXPECT_EQ(NormalizeColumnKind("JSON", false), "json");
    EXPECT_EQ(NormalizeColumnKind("BLOB", false), "blob");
    EXPECT_EQ(NormalizeColumnKind("BOOLEAN", false), "boolean");
}

TEST(SchemaKindTest, CompressedOverridesInteger) {
    EXPECT_EQ(NormalizeColumnKind("INTEGER", true), "text");
}

TEST(SchemaKindTest, UnknownTypeDefaultsToText) {
    EXPECT_EQ(NormalizeColumnKind("WEIRD", false), "text");
}
