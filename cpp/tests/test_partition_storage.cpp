#include "log_store_test_support.hpp"

#include "backlog.hpp"
#include "context.hpp"
#include "partition.hpp"
#include "utils.hpp"

#include <chrono>
#include <latch>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <sys/resource.h>

using namespace loglite;

namespace {

nlohmann::json Log(std::string timestamp, std::string message = "message") {
    return {
        {"timestamp", std::move(timestamp)}, {"message", std::move(message)}, {"level", "INFO"}};
}

Config PartitionedConfig(const std::filesystem::path& directory,
                         PartitionInterval interval = PartitionInterval::kDaily) {
    auto config = test::MakeConfig(directory);
    config.partition_interval = interval;
    return config;
}

int64_t FileBytes(const std::filesystem::path& path) {
    return test::ScalarFile(path,
                            "SELECT page_count * page_size FROM pragma_page_count(), "
                            "pragma_page_size()");
}

class PartitionStorageTest : public test::LogStoreFixture {
   protected:
    PartitionStorageTest() { cfg_.partition_interval = PartitionInterval::kDaily; }

    int64_t ReservedId() const {
        return test::ScalarFile(cfg_.db_path,
                                "SELECT reserved_id FROM partition_state WHERE id = 1");
    }

    std::filesystem::path File(std::string_view day) const {
        return directory_.path() / fmt::format("logs-daily-{}.db", day);
    }
};

}  // namespace

TEST(PartitionStorageConfigTest, OpenRejectsInvalidConfigBeforeCreatingFiles) {
    for (const auto* column : {"id", "timestamp"}) {
        SCOPED_TRACE(column);
        test::TempDirectory directory;
        auto config = PartitionedConfig(directory.path());
        config.compression = {true, {column}};
        LogStore store{config};
        EXPECT_THROW(store.Open(), std::runtime_error);
        EXPECT_TRUE(std::filesystem::is_empty(directory.path()));
    }
}

TEST(PartitionSchemeTest, UtcCalendarBoundariesIncludeOffsetsLeapDaysAndMondayWeeks) {
    struct Case {
        PartitionInterval interval;
        std::string timestamp;
        std::string filename;
        std::string until;
    };
    const std::vector<Case> cases{
        {PartitionInterval::kHourly, "2024-01-01T00:15:00+08:00", "logs-hourly-2023-12-31T16.db",
         "2023-12-31T17:00:00.000Z"},
        {PartitionInterval::kDaily, "2024-02-29T23:59:59.999999Z", "logs-daily-2024-02-29.db",
         "2024-03-01T00:00:00.000Z"},
        {PartitionInterval::kDaily, "2024-03-01T00:00:00", "logs-daily-2024-03-01.db",
         "2024-03-02T00:00:00.000Z"},
        {PartitionInterval::kWeekly, "2023-01-01T23:59:59Z", "logs-weekly-2022-12-26.db",
         "2023-01-02T00:00:00.000Z"},
        {PartitionInterval::kWeekly, "2023-01-02T00:00:00Z", "logs-weekly-2023-01-02.db",
         "2023-01-09T00:00:00.000Z"},
        {PartitionInterval::kMonthly, "2024-02-29T12:00:00Z", "logs-monthly-2024-02.db",
         "2024-03-01T00:00:00.000Z"},
        {PartitionInterval::kMonthly, "2024-12-31T23:59:59Z", "logs-monthly-2024-12.db",
         "2025-01-01T00:00:00.000Z"},
    };
    test::TempDirectory directory;
    for (const auto& item : cases) {
        SCOPED_TRACE(item.timestamp);
        const auto config = PartitionedConfig(directory.path(), item.interval);
        const PartitionScheme scheme{config};
        const auto timestamp = parse_iso8601(item.timestamp);
        ASSERT_TRUE(timestamp);
        const auto partition = scheme.Route(*timestamp);
        EXPECT_EQ(partition.path, directory.path() / item.filename);
        EXPECT_EQ(format_utc(partition.until), item.until);
        EXPECT_LE(partition.since, *timestamp);
        EXPECT_GT(partition.until, *timestamp);
        EXPECT_EQ(scheme.Route(partition.until).since, partition.until);
        const auto parsed = scheme.Parse(partition.path);
        ASSERT_TRUE(parsed);
        EXPECT_EQ(parsed->since, partition.since);
        EXPECT_EQ(parsed->until, partition.until);
        EXPECT_EQ(PartitionScheme::IntervalOf(item.filename), item.interval);
    }
    EXPECT_FALSE(PartitionScheme::IntervalOf("logs-weekly-2024-01-02.db"));
    EXPECT_FALSE(PartitionScheme::IntervalOf("logs-daily-2024-02-30.db"));
    EXPECT_FALSE(PartitionScheme::IntervalOf("logs-daily-2024-01-01.db-wal"));
    EXPECT_FALSE(PartitionScheme::IntervalOf("logs.db"));
}

TEST(PartitionSchemeTest, PlaceRewritesTimestampsToCanonicalUtcAndRoutesTheStoredInstant) {
    test::TempDirectory directory;
    const auto config = PartitionedConfig(directory.path());
    const PartitionScheme scheme{config};
    const auto ingestion = *parse_iso8601("2024-05-06T07:08:09.123456Z");
    struct Case {
        nlohmann::json value;
        std::string stored;
    };
    const std::vector<Case> cases{
        {"2024-03-01T00:15:00+08:00", "2024-02-29T16:15:00.000Z"},
        {"2024-03-01T23:59:59.999999Z", "2024-03-01T23:59:59.999Z"},
        {"2024-03-01T12:00:00", "2024-03-01T12:00:00.000Z"},
        {"not-a-time", "2024-05-06T07:08:09.123Z"},
        {12, "2024-05-06T07:08:09.123Z"},
        {nullptr, "2024-05-06T07:08:09.123Z"},
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.value.dump());
        nlohmann::json entry{{"timestamp", item.value}};
        const auto partition = scheme.Place(entry, ingestion);
        EXPECT_EQ(entry["timestamp"], item.stored);
        EXPECT_EQ(partition.path, scheme.Route(*parse_iso8601(item.stored)).path);
    }
    nlohmann::json missing{{"message", "no timestamp"}};
    scheme.Place(missing, ingestion);
    EXPECT_EQ(missing["timestamp"], "2024-05-06T07:08:09.123Z");

    // Without partitioning values are stored verbatim in logs.db.
    const auto unpartitioned = test::MakeConfig(directory.path());
    nlohmann::json verbatim{{"timestamp", "2024-03-01T00:15:00+08:00"}};
    EXPECT_EQ(PartitionScheme{unpartitioned}.Place(verbatim, ingestion).path,
              unpartitioned.db_path);
    EXPECT_EQ(verbatim["timestamp"], "2024-03-01T00:15:00+08:00");
}

TEST(PartitionSchemeTest, TimestampFiltersAreNormalizedAndPruneDisjointRanges) {
    test::TempDirectory directory;
    const auto config = PartitionedConfig(directory.path());
    const PartitionScheme scheme{config};
    const auto day = scheme.Route(*parse_iso8601("2024-01-02T12:00:00Z"));
    const auto normalized =
        scheme.NormalizeFilters({{"timestamp", ">=", "2024-01-02T08:00:00+08:00"},
                                 {"timestamp", "~=", "2024-01-02T08"},
                                 {"message", "=", "2024-01-02T08:00:00+08:00"}});
    EXPECT_EQ(normalized[0].value, "2024-01-02T00:00:00.000Z");
    EXPECT_EQ(normalized[1].value, "2024-01-02T08");
    EXPECT_EQ(normalized[2].value, "2024-01-02T08:00:00+08:00");

    struct Case {
        std::string op;
        std::string value;
        bool keep;
    };
    const std::vector<Case> cases{
        {">=", "2024-01-03T00:00:00.000Z", false}, {">=", "2024-01-02T23:59:59.999Z", true},
        {">", "2024-01-03T00:00:00.000Z", false},  {"<", "2024-01-02T00:00:00.000Z", false},
        {"<", "2024-01-02T00:00:00.001Z", true},   {"<=", "2024-01-02T00:00:00.000Z", true},
        {"<=", "2024-01-01T23:59:59.999Z", false}, {"=", "2024-01-02T00:00:00.000Z", true},
        {"=", "2024-01-03T00:00:00.000Z", false},  {"!=", "2024-01-02T00:00:00.000Z", true},
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.op + " " + item.value);
        EXPECT_EQ(scheme.MayContain(day, {{"timestamp", item.op, item.value}}), item.keep);
    }
    EXPECT_TRUE(scheme.MayContain(day, {{"message", ">=", "2025-01-01T00:00:00.000Z"}}));
}

TEST(PartitionStorageRoutingTest, EveryIntervalRoutesCustomEventTimeAtCalendarBoundaries) {
    struct Case {
        PartitionInterval interval;
        std::string before;
        std::string boundary;
        std::string before_stored;
        std::string boundary_stored;
        std::string before_file;
        std::string boundary_file;
    };
    const std::vector<Case> cases{
        {PartitionInterval::kHourly, "2024-02-29T23:59:59.999999Z", "2024-03-01T00:00:00Z",
         "2024-02-29T23:59:59.999Z", "2024-03-01T00:00:00.000Z", "logs-hourly-2024-02-29T23.db",
         "logs-hourly-2024-03-01T00.db"},
        {PartitionInterval::kDaily, "2024-03-01T07:59:59+08:00", "2024-03-01T08:00:00+08:00",
         "2024-02-29T23:59:59.000Z", "2024-03-01T00:00:00.000Z", "logs-daily-2024-02-29.db",
         "logs-daily-2024-03-01.db"},
        {PartitionInterval::kWeekly, "2023-01-01T23:59:59Z", "2023-01-02T00:00:00Z",
         "2023-01-01T23:59:59.000Z", "2023-01-02T00:00:00.000Z", "logs-weekly-2022-12-26.db",
         "logs-weekly-2023-01-02.db"},
        {PartitionInterval::kMonthly, "2024-12-31T23:59:59Z", "2025-01-01T00:00:00Z",
         "2024-12-31T23:59:59.000Z", "2025-01-01T00:00:00.000Z", "logs-monthly-2024-12.db",
         "logs-monthly-2025-01.db"},
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(ToString(item.interval));
        test::TempDirectory directory;
        auto config = PartitionedConfig(directory.path(), item.interval);
        config.log_timestamp_field = "event_time";
        config.migrations[0].rollout = {
            "CREATE TABLE TestLog (id INTEGER PRIMARY KEY, event_time TEXT NOT NULL, "
            "message TEXT NOT NULL, level TEXT NOT NULL)"};
        LogStore store{config};
        store.Open();
        store.Initialize();
        ASSERT_EQ(store.Insert({
                      {{"event_time", item.boundary}, {"message", "boundary"}, {"level", "INFO"}},
                      {{"event_time", item.before}, {"message", "before"}, {"level", "INFO"}},
                  }),
                  2);
        EXPECT_EQ(test::ScalarFile(directory.path() / item.before_file, "SELECT id FROM TestLog"),
                  1);
        EXPECT_EQ(test::ScalarFile(directory.path() / item.boundary_file, "SELECT id FROM TestLog"),
                  2);
        LogReaderPool pool{store, 1};
        const auto result = pool.UseConnection(
            [](LogReader& reader) { return reader.Query({"id", "event_time"}, {}, 10, 0); });
        EXPECT_EQ(result.results,
                  (std::vector<nlohmann::json>{{{"id", 2}, {"event_time", item.boundary_stored}},
                                               {{"id", 1}, {"event_time", item.before_stored}}}));
    }
}

TEST_F(PartitionStorageTest, InterleavedBatchCommitsEachPartitionOnceInRangeOrder) {
    std::vector<nlohmann::json> batch{
        Log("2024-01-02T01:00:00Z", "a"), Log("2024-01-01T08:00:00+08:00", "b"),
        Log("2024-01-02T02:00:00Z", "c"), Log("2024-01-01T01:00:00Z", "d")};
    std::vector<size_t> acknowledged;
    EXPECT_EQ(db_->Insert(batch, [&](size_t prefix) { acknowledged.push_back(prefix); }), 4);
    EXPECT_EQ(acknowledged, (std::vector<size_t>{2, 4}));
    std::vector<std::string> order;
    for (const auto& entry : batch) order.push_back(entry["message"]);
    EXPECT_EQ(order, (std::vector<std::string>{"b", "d", "a", "c"}));
    EXPECT_EQ(batch[0]["timestamp"], "2024-01-01T00:00:00.000Z");

    EXPECT_EQ(PartitionFiles(), (std::vector{File("2024-01-01"), File("2024-01-02")}));
    EXPECT_EQ(test::ScalarFile(File("2024-01-01"), "SELECT group_concat(id) = '1,2' FROM TestLog"),
              1);
    EXPECT_EQ(db_->GetCommittedLogId(), 4);
    EXPECT_EQ(ReservedId(), 4);
    EXPECT_EQ(test::ScalarFile(cfg_.db_path, "SELECT COUNT(*) FROM TestLog"), 0);
}

TEST_F(PartitionStorageTest, InvalidTimestampsUseIngestionTimeAndRowsMissingFieldsSkip) {
    const auto before = std::chrono::system_clock::now();
    auto numeric = Log("");
    numeric["timestamp"] = 12;
    EXPECT_EQ(db_->Insert({Log("not-a-time"),
                           numeric,
                           {{"message", "missing timestamp"}, {"level", "INFO"}},
                           {{"timestamp", "bogus"}, {"level", "INFO"}}}),
              3);
    const auto after = std::chrono::system_clock::now();
    EXPECT_EQ(PartitionFiles().size(), 1u);  // one captured ingestion time
    const auto result = Query({"timestamp"}, {}, 10, 0);
    ASSERT_EQ(result.total, 3);
    for (const auto& row : result.results) {
        const auto stored = parse_iso8601(row["timestamp"].get<std::string>());
        ASSERT_TRUE(stored);
        EXPECT_GE(*stored, std::chrono::floor<std::chrono::milliseconds>(before));
        EXPECT_LE(*stored, after);
    }
    EXPECT_EQ(db_->GetCommittedLogId(), 3);
    EXPECT_EQ(ReservedId(), 4);  // the skipped row never became committed ID 4
}

TEST_F(PartitionStorageTest, RoutingFailureRestoresAllUncommittedEntriesForRetry) {
    const std::vector<nlohmann::json> batch{Log("2024-01-02T00:00:00.000Z", "newer"),
                                            Log("2024-01-01T00:00:00.000Z", "older"), 42,
                                            Log("2024-01-03T00:00:00.000Z", "following")};
    Backlog backlog{10};
    for (const auto& entry : batch) backlog.Add(entry);
    std::vector<size_t> acknowledged;
    EXPECT_THROW(backlog.FlushCommitted([&](auto& entries, const auto& acknowledge) {
        return db_->Insert(entries, [&](size_t prefix) {
            acknowledged.push_back(prefix);
            acknowledge(prefix);
        });
    }),
                 nlohmann::json::type_error);

    auto restored = backlog.Flush();
    ASSERT_EQ(restored, batch);
    EXPECT_TRUE(acknowledged.empty());
    EXPECT_TRUE(PartitionFiles().empty());
    EXPECT_EQ(ReservedId(), 0);

    // Remove the malformed entry and retry the preserved logs.
    restored.erase(restored.begin() + 2);
    for (auto& entry : restored) backlog.Add(std::move(entry));
    EXPECT_EQ(backlog.FlushCommitted([&](auto& entries, const auto& acknowledge) {
        return db_->Insert(entries, acknowledge);
    }),
              3);
    EXPECT_TRUE(backlog.Flush().empty());
    EXPECT_EQ(Query({"message"}, {}, 10, 0).results,
              (std::vector<nlohmann::json>{
                  {{"message", "following"}}, {{"message", "newer"}}, {{"message", "older"}}}));
}

TEST_F(PartitionStorageTest, PartialFailureAcknowledgesCommittedPrefixAndNotifiesDurableRows) {
    cfg_.migrations.push_back(
        {2,
         {"CREATE TRIGGER reject_log BEFORE INSERT ON TestLog WHEN NEW.message = 'reject' "
          "BEGIN SELECT RAISE(ABORT, 'injected partition failure'); END"},
         {"DROP TRIGGER reject_log"}});
    ASSERT_TRUE(db_->Rollout());
    Backlog backlog{10};
    backlog.Add(Log("2024-01-01T00:00:00Z", "accepted"));
    backlog.Add(Log("2024-01-02T00:00:00Z", "reject"));
    backlog.Add(Log("2024-01-03T00:00:00Z", "following"));
    LogNotifier notifier;
    asio::thread_pool executor{1};
    ServerContext context{cfg_,
                          *db_,
                          *pool_,
                          backlog,
                          notifier,
                          asio::make_strand(executor.get_executor()),
                          executor.get_executor()};
    EXPECT_THROW(context.FlushBacklog(), std::runtime_error);
    EXPECT_EQ(backlog.Size(), 2u);
    EXPECT_EQ(db_->EstimateLogRowCount(), 1);
    EXPECT_EQ(db_->GetCommittedLogId(), 1);
    EXPECT_EQ(notifier.GetLastId(), 1);
    EXPECT_EQ(ReservedId(), 2);
    EXPECT_TRUE(db_->Rollback(2, true));
    EXPECT_EQ(context.FlushBacklog(), 2);
    EXPECT_EQ(backlog.Size(), 0u);
    EXPECT_EQ(notifier.GetLastId(), 4);
    EXPECT_EQ(Query({"*"}, {}, 10, 0).total, 3);
    executor.join();
}

TEST_F(PartitionStorageTest, RestartNeverReusesReservedIdsAfterEveryFileExpired) {
    cfg_.vacuum_max_days = 1;
    EXPECT_EQ(db_->Insert({Log("2000-01-01T00:00:00Z"), Log("2000-01-02T00:00:00Z")}), 2);
    db_->Maintain();
    EXPECT_TRUE(PartitionFiles().empty());
    EXPECT_EQ(db_->EstimateLogRowCount(), 0);
    OpenStore();
    EXPECT_EQ(db_->GetCommittedLogId(), 0);
    EXPECT_EQ(db_->Insert({Log("2024-01-03T00:00:00Z")}), 1);
    EXPECT_EQ(db_->GetCommittedLogId(), 3);
    EXPECT_EQ(Query({"id"}, {}, 10, 0).results[0]["id"], 3);
}

TEST_F(PartitionStorageTest, MigrationFanoutAndFutureFilesUseSortedApprovedHistory) {
    EXPECT_EQ(db_->Insert({Log("2024-01-01T00:00:00Z"), Log("2024-01-02T00:00:00Z")}), 2);
    cfg_.migrations.insert(cfg_.migrations.begin(), {2,
                                                     {"ALTER TABLE TestLog ADD COLUMN extra TEXT"},
                                                     {"ALTER TABLE TestLog DROP COLUMN extra"}});
    EXPECT_TRUE(db_->Rollout());
    EXPECT_FALSE(db_->Rollout());
    auto row = Log("2024-01-03T00:00:00Z");
    row["extra"] = "future";
    EXPECT_EQ(db_->Insert({row}), 1);
    EXPECT_EQ(Query({"extra"}, {{"id", "=", 3}}, 10, 0).results[0]["extra"], "future");
    EXPECT_TRUE(db_->Rollback(2, true));
    EXPECT_FALSE(db_->Rollback(2, true));
    OpenStore();
    EXPECT_EQ(Query({"*"}, {}, 10, 0).total, 3);
}

TEST_F(PartitionStorageTest, AgeRetentionRemovesExpiredFilesWholeAndKeepsLateNewIdsMonotonic) {
    cfg_.vacuum_max_days = 1;
    cfg_.vacuum_max_size_bytes = std::numeric_limits<int64_t>::max();
    EXPECT_EQ(db_->Insert(
                  {Log("2000-01-01T00:00:00Z"), Log(format_utc(std::chrono::system_clock::now()))}),
              2);
    const auto stale = File("2000-01-01");
    ASSERT_TRUE(std::filesystem::exists(stale));
    db_->Maintain();
    EXPECT_FALSE(std::filesystem::exists(stale));
    EXPECT_FALSE(std::filesystem::exists(stale.string() + "-wal"));
    EXPECT_FALSE(std::filesystem::exists(stale.string() + "-shm"));
    EXPECT_EQ(db_->EstimateLogRowCount(), 1);
    EXPECT_EQ(Query({"*"}, {}, 10, 0).total, 1);
    OpenStore();
    EXPECT_EQ(db_->Insert({Log("2000-01-01T00:00:00Z", "late recreated")}), 1);
    EXPECT_EQ(db_->GetCommittedLogId(), 3);
}

TEST_F(PartitionStorageTest, AgeRetentionTrimsRowsOnlyInTheBoundaryFile) {
    cfg_.vacuum_max_days = 1;
    cfg_.vacuum_max_size_bytes = std::numeric_limits<int64_t>::max();
    const auto expiry = std::chrono::system_clock::now() - std::chrono::hours{24};
    const auto boundary = db_->scheme().Route(expiry);
    // Retention compares whole seconds; leave room on both sides of the cutoff.
    if (expiry - boundary.since < std::chrono::seconds{2} ||
        boundary.until - expiry < std::chrono::seconds{4})
        GTEST_SKIP() << "the cutoff is too close to a day boundary";
    ASSERT_EQ(db_->Insert({Log(format_utc(boundary.since), "expired"),
                           Log(format_utc(expiry + std::chrono::seconds{2}), "kept"),
                           Log(format_utc(std::chrono::system_clock::now()), "fresh")}),
              3);
    db_->Maintain();
    EXPECT_TRUE(std::filesystem::exists(boundary.path));
    EXPECT_EQ(db_->EstimateLogRowCount(), 2);
    const auto result = Query({"message"}, {}, 10, 0);
    EXPECT_EQ(result.total, 2);
    EXPECT_EQ(result.results.back()["message"], "kept");
}

TEST_F(PartitionStorageTest, SizeRetentionDropsOldestFilesWholeThenTrimsTheBoundaryFile) {
    cfg_.vacuum_max_days = 36500;
    std::vector<nlohmann::json> rows;
    for (const auto* day : {"2024-01-01", "2024-01-02", "2024-01-03"})
        for (int index = 0; index < 10; ++index)
            rows.push_back(
                Log(fmt::format("{}T00:00:{:02d}Z", day, index), std::string(4096, 'x')));
    ASSERT_EQ(db_->Insert(rows), 30);
    const auto oldest = FileBytes(File("2024-01-01"));
    const auto boundary = FileBytes(File("2024-01-02"));
    const auto live = oldest + boundary + FileBytes(File("2024-01-03"));
    cfg_.vacuum_max_size_bytes = live - 1;
    cfg_.vacuum_target_size_bytes = live - oldest - boundary / 2 - 1;
    db_->Maintain();
    EXPECT_FALSE(std::filesystem::exists(File("2024-01-01")));
    EXPECT_EQ(test::ScalarFile(File("2024-01-02"), "SELECT COUNT(*) FROM TestLog"), 5);
    EXPECT_EQ(test::ScalarFile(File("2024-01-02"), "SELECT MIN(id) FROM TestLog"), 16);
    EXPECT_EQ(test::ScalarFile(File("2024-01-03"), "SELECT COUNT(*) FROM TestLog"), 10);
    EXPECT_EQ(db_->EstimateLogRowCount(), 15);
    EXPECT_EQ(Query({"id"}, {}, 100, 0).total, 15);
}

TEST_F(PartitionStorageTest, MaintainDefersFileRemovalWhileAReadLeaseIsHeld) {
    cfg_.vacuum_max_days = 1;
    cfg_.vacuum_max_size_bytes = std::numeric_limits<int64_t>::max();
    ASSERT_EQ(db_->Insert({Log("2000-01-01T00:00:00Z"),
                           Log(format_utc(std::chrono::system_clock::now()), "fresh")}),
              2);
    const auto stale = File("2000-01-01");
    std::latch leased{1};
    std::latch release{1};
    std::jthread reader{[&] {
        const auto lease = db_->ReadLease();
        leased.count_down();
        release.wait();
    }};
    leased.wait();
    db_->Maintain();  // must not wait for the reader
    EXPECT_TRUE(std::filesystem::exists(stale));
    EXPECT_EQ(db_->Partitions().size(), 2u);
    release.count_down();
    reader.join();
    db_->Maintain();
    EXPECT_FALSE(std::filesystem::exists(stale));
    EXPECT_EQ(Query({"message"}, {}, 10, 0).results,
              (std::vector<nlohmann::json>{{{"message", "fresh"}}}));
}

TEST_F(PartitionStorageTest, MissingControlAllocatorCannotBeRebuiltFromSurvivingRows) {
    ASSERT_EQ(db_->Insert({Log("2024-01-01T00:00:00Z"), Log("2024-01-02T00:00:00Z")}), 2);
    pool_.reset();
    db_.reset();
    test::ExecuteFile(cfg_.db_path, "DROP TABLE partition_state");
    LogStore damaged{cfg_};
    EXPECT_THROW(damaged.Open(), std::runtime_error);
    EXPECT_EQ(test::ScalarFile(File("2024-01-01"), "SELECT MAX(id) FROM TestLog"), 1);
}

TEST_F(PartitionStorageTest, InitializedIntervalIsImmutableBeforeAnyRowsExist) {
    ASSERT_TRUE(PartitionFiles().empty());
    pool_.reset();
    db_.reset();
    for (const auto interval : {PartitionInterval::kNone, PartitionInterval::kHourly,
                                PartitionInterval::kWeekly, PartitionInterval::kMonthly}) {
        SCOPED_TRACE(ToString(interval));
        cfg_.partition_interval = interval;
        LogStore changed{cfg_};
        EXPECT_THROW(changed.Open(), std::runtime_error);
    }
}

TEST_F(PartitionStorageTest, PartitionFilesRequireTheirControlDatabase) {
    ASSERT_EQ(db_->Insert({Log("2024-01-01T00:00:00Z")}), 1);
    pool_.reset();
    db_.reset();
    std::filesystem::remove(cfg_.db_path);
    LogStore orphan{cfg_};
    EXPECT_THROW(orphan.Open(), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(cfg_.db_path));
}

TEST(LogStoreCompatibilityTest, PartitioningRejectsAnExistingDatabaseThatHoldsLogs) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    {
        LogStore legacy{config};
        legacy.Open();
        legacy.Initialize();
        ASSERT_EQ(legacy.Insert({Log("2023-01-01T00:00:00Z", "legacy")}), 1);
    }
    config.partition_interval = PartitionInterval::kDaily;
    LogStore store{config};
    try {
        store.Open();
        FAIL() << "expected the breaking-change guard";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string_view{error.what()}.find("not backwards compatible"),
                  std::string_view::npos);
    }
    EXPECT_EQ(test::ScalarFile(config.db_path, "SELECT COUNT(*) FROM TestLog"), 1);
    EXPECT_EQ(test::ScalarFile(config.db_path,
                               "SELECT COUNT(*) FROM sqlite_master WHERE name = 'partition_state'"),
              0);
}

TEST(LogStoreCompatibilityTest, PartitioningAdoptsAnExistingDatabaseWithoutLogs) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    {
        LogStore legacy{config};
        legacy.Open();
        legacy.Initialize();
    }
    config.partition_interval = PartitionInterval::kDaily;
    LogStore store{config};
    store.Open();
    store.Initialize();
    EXPECT_EQ(store.Insert({Log("2024-01-01T00:00:00Z")}), 1);
    EXPECT_EQ(store.GetCommittedLogId(), 1);
}

TEST(LogStoreMigrationTest, UnpartitionedSeedMigrationsRemainSupported) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    config.migrations[0].rollout.push_back(
        "INSERT INTO TestLog (timestamp, message, level) VALUES ('2024-01-01T00:00:00Z', 'seed', "
        "'INFO')");
    LogStore store{config};
    store.Open();
    store.Initialize();
    LogReaderPool pool{store, 1};
    EXPECT_EQ(pool.UseConnection([](LogReader& reader) {
        return reader.Query({"id", "message"}, {}, 10, 0).results;
    }),
              (std::vector<nlohmann::json>{{{"id", 1}, {"message", "seed"}}}));
}

TEST_F(PartitionStorageTest, InterruptedMigrationFanoutCanBeRepairedOfflineInEitherDirection) {
    ASSERT_EQ(db_->Insert({Log("2024-01-01T00:00:00Z"), Log("2024-01-02T00:00:00Z")}), 2);
    const auto first = File("2024-01-01");
    const auto second = File("2024-01-02");
    cfg_.auto_rollout = false;
    cfg_.migrations.push_back({2,
                               {"ALTER TABLE TestLog ADD COLUMN extra TEXT"},
                               {"ALTER TABLE TestLog DROP COLUMN extra"}});
    pool_.reset();
    db_.reset();
    test::ExecuteFile(second,
                      "CREATE TRIGGER fail_rollout BEFORE INSERT ON versions "
                      "WHEN NEW.version = 2 BEGIN SELECT RAISE(ABORT, 'rollout failed'); END");
    {
        LogStore migration{cfg_};
        migration.Open();
        EXPECT_THROW(migration.Rollout(), std::runtime_error);
    }
    EXPECT_EQ(test::ScalarFile(first, "SELECT MAX(version) FROM versions"), 2);
    EXPECT_EQ(test::ScalarFile(second, "SELECT MAX(version) FROM versions"), 1);
    EXPECT_EQ(test::ScalarFile(cfg_.db_path, "SELECT MAX(version) FROM versions"), 1);
    {
        LogStore incomplete{cfg_};
        incomplete.Open();
        EXPECT_THROW(incomplete.Initialize(), std::runtime_error);
    }
    test::ExecuteFile(second, "DROP TRIGGER fail_rollout");
    {
        LogStore migration{cfg_};
        migration.Open();
        ASSERT_TRUE(migration.Rollout());
        migration.Initialize();
        EXPECT_EQ(migration.EstimateLogRowCount(), 2);
    }
    test::ExecuteFile(second,
                      "CREATE TRIGGER fail_rollback BEFORE DELETE ON versions "
                      "WHEN OLD.version = 2 BEGIN SELECT RAISE(ABORT, 'rollback failed'); END");
    {
        LogStore migration{cfg_};
        migration.Open();
        EXPECT_THROW(migration.Rollback(2, true), std::runtime_error);
    }
    EXPECT_EQ(test::ScalarFile(first, "SELECT MAX(version) FROM versions"), 1);
    EXPECT_EQ(test::ScalarFile(second, "SELECT MAX(version) FROM versions"), 2);
    EXPECT_EQ(test::ScalarFile(cfg_.db_path, "SELECT MAX(version) FROM versions"), 2);
    test::ExecuteFile(second, "DROP TRIGGER fail_rollback");
    {
        LogStore migration{cfg_};
        migration.Open();
        ASSERT_TRUE(migration.Rollback(2, true));
        migration.Initialize();
        EXPECT_EQ(migration.EstimateLogRowCount(), 2);
        EXPECT_EQ(migration.Insert({Log("2024-01-03T00:00:00Z")}), 1);
    }
    EXPECT_EQ(test::ScalarFile(File("2024-01-03"), "SELECT MAX(version) FROM versions"), 1);
}

TEST_F(PartitionStorageTest, IncrementalRetentionRemovesExpiredFilesWithoutSpendingTheRowBudget) {
    cfg_.sqlite_params["auto_vacuum"] = "INCREMENTAL";
    cfg_.task_vacuum_max_size = 1;
    cfg_.vacuum_max_days = 1;
    cfg_.vacuum_max_size_bytes = std::numeric_limits<int64_t>::max();
    OpenStore();
    std::vector<nlohmann::json> rows;
    for (int index = 0; index < 300; ++index)
        rows.push_back(Log(index < 150 ? "2000-01-01T00:00:00Z" : "2000-01-02T00:00:00Z",
                           std::string(8192, 'x')));
    rows.push_back(Log(format_utc(std::chrono::system_clock::now()), "fresh"));
    ASSERT_EQ(db_->Insert(rows), 301);
    db_->Maintain();
    EXPECT_EQ(PartitionFiles().size(), 1u);
    EXPECT_EQ(db_->EstimateLogRowCount(), 1);
    EXPECT_EQ(Query({"message"}, {}, 10, 0).results,
              (std::vector<nlohmann::json>{{{"message", "fresh"}}}));
}

class CompressedPartitionStorageTest : public PartitionStorageTest {
   protected:
    CompressedPartitionStorageTest() {
        cfg_.compression = {true, {"level"}};
        cfg_.migrations[0].rollout = {
            "CREATE TABLE TestLog (id INTEGER PRIMARY KEY, timestamp TEXT NOT NULL, "
            "message TEXT NOT NULL, level INTEGER NOT NULL)"};
    }
};

TEST_F(CompressedPartitionStorageTest, RetryReloadsDictionaryAfterOnePartitionCommitted) {
    cfg_.migrations.push_back(
        {2,
         {"CREATE TRIGGER reject_log BEFORE INSERT ON TestLog "
          "WHEN NEW.message = 'reject' BEGIN SELECT RAISE(ABORT, 'reject'); END"},
         {"DROP TRIGGER reject_log"}});
    ASSERT_TRUE(db_->Rollout());
    Backlog backlog{10};
    auto accepted = Log("2024-01-01T00:00:00Z", "accepted");
    auto rejected = Log("2024-01-02T00:00:00Z", "reject");
    rejected["level"] = "FAILED";
    auto following = Log("2024-01-02T01:00:00Z", "following");
    following["level"] = "ERROR";
    for (const auto& row : {accepted, rejected, following}) backlog.Add(row);
    LogNotifier notifier;
    asio::thread_pool executor{1};
    ServerContext context{cfg_,
                          *db_,
                          *pool_,
                          backlog,
                          notifier,
                          asio::make_strand(executor.get_executor()),
                          executor.get_executor()};
    EXPECT_THROW(context.FlushBacklog(), std::runtime_error);
    EXPECT_EQ(backlog.Size(), 2u);
    EXPECT_EQ(notifier.GetLastId(), 1);
    EXPECT_EQ(Query({"level"}, {}, 10, 0).results,
              (std::vector<nlohmann::json>{{{"level", "INFO"}}}));
    ASSERT_TRUE(db_->Rollback(2, true));
    EXPECT_EQ(context.FlushBacklog(), 2);
    EXPECT_EQ(backlog.Size(), 0u);
    EXPECT_EQ(Query({"level"}, {}, 10, 0).results,
              (std::vector<nlohmann::json>{
                  {{"level", "ERROR"}}, {{"level", "FAILED"}}, {{"level", "INFO"}}}));
    EXPECT_EQ(Query({"id"}, {{"level", "=", "FAILED"}}, 10, 0).total, 1);
    executor.join();
}

TEST_F(CompressedPartitionStorageTest, FiltersUseEachFilesDictionaryForCompressedValues) {
    auto first_error = Log("2024-01-01T00:00:00Z", "first error");
    first_error["level"] = "ERROR";
    auto first_info = Log("2024-01-01T01:00:00Z", "first info");
    auto second_info = Log("2024-01-02T00:00:00Z", "second info");
    auto second_error = Log("2024-01-02T01:00:00Z", "second error");
    second_error["level"] = "ERROR";
    ASSERT_EQ(db_->Insert({first_error, first_info, second_info, second_error}), 4);
    const auto error_id =
        "SELECT value_id FROM column_dictionary WHERE column = 'level' AND value = 'ERROR'";
    EXPECT_NE(test::ScalarFile(File("2024-01-01"), error_id),
              test::ScalarFile(File("2024-01-02"), error_id));
    const auto errors = Query({"id", "level"}, {{"level", "=", "ERROR"}}, 10, 0);
    EXPECT_EQ(errors.total, 2);
    EXPECT_EQ(errors.results, (std::vector<nlohmann::json>{{{"id", 4}, {"level", "ERROR"}},
                                                           {{"id", 1}, {"level", "ERROR"}}}));
}

TEST_F(CompressedPartitionStorageTest, DictionaryValuesPreserveEmbeddedNullsAcrossRestart) {
    const std::string level{"IN\0FO", 5};
    auto first = Log("2024-01-01T00:00:00Z", "first");
    first["level"] = level;
    ASSERT_EQ(db_->Insert({first}), 1);
    EXPECT_EQ(Query({"level"}, {{"level", "=", level}}, 10, 0).results,
              (std::vector<nlohmann::json>{{{"level", level}}}));
    OpenStore();
    auto second = Log("2024-01-01T01:00:00Z", "second");
    second["level"] = level;
    ASSERT_EQ(db_->Insert({second}), 1);
    const auto result = Query({"level"}, {{"level", "=", level}}, 10, 0);
    ASSERT_EQ(result.total, 2);
    EXPECT_EQ(result.results,
              (std::vector<nlohmann::json>{{{"level", level}}, {{"level", level}}}));
    EXPECT_EQ(test::ScalarFile(File("2024-01-01"),
                               "SELECT COUNT(*) FROM column_dictionary WHERE column = 'level'"),
              1);
}

TEST_F(PartitionStorageTest, ManyHourlyFilesStayWithinAProcessFileDescriptorLimit) {
    // Restoring the process limit is unconditional, including failed assertions.
    struct DescriptorLimit {
        rlimit original{};
        bool changed{};
        ~DescriptorLimit() {
            if (changed) setrlimit(RLIMIT_NOFILE, &original);
        }
    } limit;
    ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &limit.original), 0);
    auto reduced = limit.original;
    reduced.rlim_cur = std::min(rlim_t{64}, reduced.rlim_cur);
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &reduced), 0);
    limit.changed = true;

    // Use a separate empty directory: initialized intervals are immutable.
    test::TempDirectory directory;
    const auto config = PartitionedConfig(directory.path(), PartitionInterval::kHourly);
    LogStore store{config};
    store.Open();
    store.Initialize();
    std::vector<nlohmann::json> rows;
    const auto start = *parse_iso8601("2024-01-01T00:00:00Z");
    for (int hour = 0; hour < 100; ++hour)
        rows.push_back(Log(format_utc(start + std::chrono::hours{hour})));
    ASSERT_EQ(store.Insert(rows), 100);
    LogReaderPool pool{store, 1};
    const auto result =
        pool.UseConnection([](LogReader& reader) { return reader.Query({"id"}, {}, 100, 0); });
    ASSERT_EQ(result.total, 100);
    ASSERT_EQ(result.results.size(), 100u);
    EXPECT_EQ(result.results.front()["id"], 100);
    EXPECT_EQ(result.results.back()["id"], 1);
    EXPECT_EQ(store.GetCommittedLogId(), 100);
}
