#include "log_store_test_support.hpp"

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace loglite;

namespace {

class PartitionQueryTest : public test::LogStoreFixture {
   protected:
    PartitionQueryTest() {
        cfg_.partition_interval = PartitionInterval::kDaily;
        cfg_.compression = {true, {"level"}};
        cfg_.migrations[0].rollout = {
            "CREATE TABLE TestLog (id INTEGER PRIMARY KEY, timestamp TEXT NOT NULL, "
            "message TEXT NOT NULL, level INTEGER NOT NULL, service TEXT)"};
    }

    // One insert per row, so IDs follow the call order rather than the partition order.
    void InsertEach(std::initializer_list<nlohmann::json> rows) {
        for (const auto& row : rows) ASSERT_EQ(db_->Insert({row}), 1);
    }

    LogIdQueryResult QueryLogIdRange(const std::vector<std::string>& fields, int64_t since,
                                     int64_t until, int limit) {
        return pool_->UseConnection(
            [&](LogReader& reader) { return reader.QueryLogIdRange(fields, since, until, limit); });
    }

    // Opening a missing partition file fails, which proves a query never touched it.
    void MakeUnavailable(std::string_view day) {
        const auto path = directory_.path() / fmt::format("logs-daily-{}.db", day);
        std::filesystem::rename(path, path.string() + ".unavailable");
    }

    static nlohmann::json Log(std::string timestamp, std::string message,
                              std::string level = "INFO") {
        return {{"timestamp", std::move(timestamp)},
                {"message", std::move(message)},
                {"level", std::move(level)}};
    }

    static std::vector<int64_t> Ids(const auto& result) {
        std::vector<int64_t> ids;
        for (const auto& row : result.results) ids.push_back(row["id"].template get<int64_t>());
        return ids;
    }
};

TEST_F(PartitionQueryTest, GlobalPaginationReadsFilesNewestFirstWithLocalDictionaries) {
    InsertEach({Log("2024-01-02T08:00:00+08:00", "offset", "ERROR"),
                Log("2024-01-01T23:00:00Z", "utc"), Log("2024-01-02T00:00:00Z", "tie lower"),
                Log("2024-01-02T00:00:00Z", "tie higher"),
                Log("2024-01-01T22:00:00Z", "older", "ERROR")});

    const auto full = Query({"id"}, {}, 10, 0);
    EXPECT_EQ(full.total, 5);
    EXPECT_EQ(Ids(full), (std::vector<int64_t>{4, 3, 1, 2, 5}));
    EXPECT_EQ(Ids(Query({"id"}, {}, 2, 1)), (std::vector<int64_t>{3, 1}));
    EXPECT_EQ(Ids(Query({"id"}, {}, 2, 3)), (std::vector<int64_t>{2, 5}));
    EXPECT_TRUE(Query({"id"}, {}, 2, std::numeric_limits<int>::max()).results.empty());

    const auto filtered = Query({"message", "level"}, {{"level", "=", "ERROR"}}, 5, 0);
    EXPECT_EQ(filtered.total, 2);
    EXPECT_EQ(filtered.results,
              (std::vector<nlohmann::json>{{{"message", "offset"}, {"level", "ERROR"}},
                                           {{"message", "older"}, {"level", "ERROR"}}}));
    EXPECT_EQ(Query({"id"}, {{"message", "~=", "tie"}}, 10, 0).total, 2);
    EXPECT_EQ(Ids(Query({"id"}, {{"level", "=", "ERROR"}}, 1, 1)), (std::vector<int64_t>{5}));
}

TEST_F(PartitionQueryTest, TimestampFiltersAreNormalizedLikeStoredValues) {
    InsertEach({Log("2024-01-01T23:59:59.999999Z", "before"), Log("2024-01-02T00:00:00Z", "at"),
                Log("2024-01-02T00:00:00.5Z", "after")});
    for (const auto* value : {"2024-01-02T00:00:00Z", "2024-01-02T08:00:00+08:00",
                              "2024-01-02T00:00:00", "2024-01-02T00:00:00.000Z"}) {
        SCOPED_TRACE(value);
        EXPECT_EQ(Query({"id"}, {{"timestamp", ">=", value}}, 10, 0).total, 2);
        EXPECT_EQ(Query({"id"}, {{"timestamp", "<", value}}, 10, 0).total, 1);
        EXPECT_EQ(Ids(Query({"id"}, {{"timestamp", "=", value}}, 10, 0)),
                  (std::vector<int64_t>{2}));
    }
    EXPECT_EQ(Query({"timestamp"}, {{"message", "=", "before"}}, 10, 0).results[0]["timestamp"],
              "2024-01-01T23:59:59.999Z");
    EXPECT_EQ(Query({"id"}, {{"timestamp", "~=", "T00:00:00"}}, 10, 0).total, 2);
}

TEST_F(PartitionQueryTest, TimestampFiltersPruneFilesOutsideTheirRange) {
    InsertEach({Log("2024-01-01T12:00:00Z", "first"), Log("2024-01-02T12:00:00Z", "second"),
                Log("2024-01-03T12:00:00Z", "third")});
    MakeUnavailable("2024-01-01");
    MakeUnavailable("2024-01-03");
    const auto result = Query(
        {"message"},
        {{"timestamp", ">=", "2024-01-02T00:00:00Z"}, {"timestamp", "<", "2024-01-03T00:00:00Z"}},
        10, 0);
    EXPECT_EQ(result.total, 1);
    EXPECT_EQ(result.results, (std::vector<nlohmann::json>{{{"message", "second"}}}));
    EXPECT_THROW(Query({"id"}, {{"timestamp", ">=", "2024-01-01T00:00:00Z"}}, 10, 0),
                 std::runtime_error);
}

TEST_F(PartitionQueryTest, UnfilteredPagesSkipWholeFilesByRowCount) {
    InsertEach({Log("2024-01-01T00:00:00Z", "a"), Log("2024-01-01T01:00:00Z", "b"),
                Log("2024-01-02T00:00:00Z", "c"), Log("2024-01-02T01:00:00Z", "d"),
                Log("2024-01-03T00:00:00Z", "e")});
    MakeUnavailable("2024-01-01");
    MakeUnavailable("2024-01-03");
    // Offset 1 passes the newest file; the page fills before the oldest one.
    const auto page = Query({"message"}, {}, 2, 1);
    EXPECT_EQ(page.total, 5);
    EXPECT_EQ(page.results, (std::vector<nlohmann::json>{{{"message", "d"}}, {{"message", "c"}}}));
    EXPECT_THROW(Query({"id"}, {}, 3, 1), std::runtime_error);
}

TEST_F(PartitionQueryTest, IdRangePagesRespectFixedUpperBoundDespiteLaterLateArrival) {
    InsertEach({Log("2024-01-03T00:00:00Z", "first", "ERROR"),
                Log("2024-01-02T00:00:00Z", "second"), Log("2024-01-01T00:00:00Z", "third")});
    auto page = QueryLogIdRange({"message", "level"}, 0, 3, 2);
    EXPECT_EQ(page.last_id, 2);
    EXPECT_EQ(page.results,
              (std::vector<nlohmann::json>{{{"message", "first"}, {"level", "ERROR"}},
                                           {{"message", "second"}, {"level", "INFO"}}}));

    ASSERT_EQ(db_->Insert({Log("2023-01-01T00:00:00Z", "later late row", "ERROR")}), 1);
    const auto remainder = QueryLogIdRange({"message"}, page.last_id, 3, 2);
    EXPECT_EQ(remainder.last_id, 3);
    EXPECT_EQ(remainder.results, (std::vector<nlohmann::json>{{{"message", "third"}}}));
    const auto empty = QueryLogIdRange({"message"}, remainder.last_id, 3, 2);
    EXPECT_EQ(empty.last_id, 3);
    EXPECT_TRUE(empty.results.empty());
    EXPECT_EQ(QueryLogIdRange({"message"}, 3, 4, 2).results,
              (std::vector<nlohmann::json>{{{"message", "later late row"}}}));
}

TEST_F(PartitionQueryTest, IdPagesSkipFilesWhoseKnownIdsPrecedeTheCursor) {
    InsertEach({Log("2024-01-01T00:00:00Z", "first"), Log("2024-01-02T00:00:00Z", "second")});
    MakeUnavailable("2024-01-01");
    const auto page = QueryLogIdRange({"message"}, 1, 2, 1);
    EXPECT_EQ(page.last_id, 2);
    EXPECT_EQ(page.results, (std::vector<nlohmann::json>{{{"message", "second"}}}));
    EXPECT_THROW(QueryLogIdRange({"id"}, 0, 2, 1), std::runtime_error);
}

TEST_F(PartitionQueryTest, EmptyStoreStillValidatesProjectionAndFilters) {
    EXPECT_EQ(Query({"*"}, {}, 10, 0).total, 0);
    EXPECT_THROW(Query({"unknown"}, {}, 10, 0), std::runtime_error);
    EXPECT_THROW(Query({"id"}, {{"unknown", "=", "x"}}, 10, 0), std::runtime_error);
    EXPECT_THROW(Query({"id"}, {{"level", "LIKE", "x"}}, 10, 0), std::runtime_error);
}

TEST_F(PartitionQueryTest, ZeroAndNegativeLimitsKeepSQLiteSemantics) {
    InsertEach({Log("2024-01-01T00:00:00Z", "first"), Log("2024-01-02T00:00:00Z", "second")});
    const auto zero = Query({"id"}, {}, 0, 0);
    EXPECT_EQ(zero.total, 2);
    EXPECT_TRUE(zero.results.empty());
    EXPECT_EQ(Ids(Query({"id"}, {}, -1, 0)), (std::vector<int64_t>{2, 1}));
    EXPECT_EQ(Ids(Query({"id"}, {{"level", "=", "INFO"}}, -1, 1)), (std::vector<int64_t>{1}));
    EXPECT_TRUE(QueryLogIdRange({"id"}, 0, 2, 0).results.empty());
    const auto unlimited = QueryLogIdRange({"id"}, 0, 2, -1);
    EXPECT_EQ(Ids(unlimited), (std::vector<int64_t>{1, 2}));
    EXPECT_EQ(unlimited.last_id, 2);
}

}  // namespace
