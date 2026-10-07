#include <gtest/gtest.h>

#include "handlers/common.hpp"

using namespace loglite;
using namespace loglite::handlers;

// ── Filter expression parsing ─────────────────────────────────────────────────

TEST(FilterParseTest, OperatorsAreRecognizedWithoutLosingValues) {
    for (const auto* op : {"=", "!=", ">", ">=", "<", "<=", "~="}) {
        SCOPED_TRACE(op);
        auto filters = ParseQueryFilters("level", std::string{op} + "ERROR");
        ASSERT_EQ(filters.size(), 1u);
        EXPECT_EQ(filters[0].field, "level");
        EXPECT_EQ(filters[0].op, op);
        EXPECT_EQ(filters[0].value, "ERROR");
    }
    EXPECT_TRUE(ParseQueryFilters("level", "").empty());
}

TEST(FilterParseTest, MultipleOperatorsKeepTheirOwnValues) {
    auto filters = ParseQueryFilters("timestamp", ">=2024-01-01T00:00:00,<=2024-01-02T00:00:00");
    ASSERT_EQ(filters.size(), 2u);
    EXPECT_EQ(filters[0].field, "timestamp");
    EXPECT_EQ(filters[0].op, ">=");
    EXPECT_EQ(filters[0].value, "2024-01-01T00:00:00");
    EXPECT_EQ(filters[1].field, "timestamp");
    EXPECT_EQ(filters[1].op, "<=");
    EXPECT_EQ(filters[1].value, "2024-01-02T00:00:00");
}

// ── Response helpers ──────────────────────────────────────────────────────────

TEST(ResponseHelperTest, JSONResponsesPreserveStatusHeadersBodyAndConnectionPolicy) {
    for (bool keep_alive : {false, true}) {
        SCOPED_TRACE(keep_alive);
        http::request<http::string_body> raw{http::verb::get, "/test", 11};
        raw.keep_alive(keep_alive);
        const Request req{std::move(raw)};
        const auto verify = [&](const auto& response, http::status status,
                                const nlohmann::json& body) {
            EXPECT_EQ(response.result(), status);
            EXPECT_EQ(response[http::field::content_type], "application/json");
            EXPECT_EQ(response[http::field::access_control_allow_origin], "https://example.com");
            EXPECT_EQ(response.keep_alive(), keep_alive);
            EXPECT_EQ(response[http::field::content_length],
                      std::to_string(response.body().size()));
            EXPECT_EQ(nlohmann::json::parse(response.body()), body);
        };
        verify(MakeOKResp({{"key", "value"}}, req, "https://example.com"), http::status::ok,
               {{"key", "value"}});
        verify(MakeFailResp(404, "not found", req, "https://example.com"), http::status::not_found,
               {{"error", "not found"}});
        verify(MakeNotAvailableResp({{"msg", "busy"}}, req, "https://example.com"),
               http::status::service_unavailable, {{"msg", "busy"}});
    }
}
