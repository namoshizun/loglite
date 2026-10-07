#include "backlog.hpp"
#include "log_store_test_support.hpp"

using namespace loglite;

namespace {

nlohmann::json Log(nlohmann::json timestamp, std::string message = "log") {
    return {
        {"timestamp", std::move(timestamp)}, {"message", std::move(message)}, {"level", "INFO"}};
}

std::vector<std::string> Messages(const std::vector<Database::LogRow>& rows) {
    std::vector<std::string> messages;
    for (const auto& row : rows) messages.push_back(row.values.at("message").get<std::string>());
    return messages;
}

}  // namespace

TEST(LiveStorageTest, PureFieldResolutionValidatesWithoutOpeningStorage) {
    const std::vector<ColumnInfo> schema{{"id", "INTEGER"}, {"message", "TEXT"}};
    EXPECT_EQ(ResolveLogFields(schema, {"*"}), (std::vector<std::string>{"id", "message"}));
    EXPECT_EQ(ResolveLogFields(schema, {"message"}), (std::vector<std::string>{"message"}));
    for (const auto& fields :
         {std::vector<std::string>{""}, {"message", ""}, {" message"}, {"missing"}})
        EXPECT_THROW((void)ResolveLogFields(schema, fields), std::runtime_error);
}

class LiveStorageModesTest : public ::testing::TestWithParam<PartitionInterval> {};

TEST_P(LiveStorageModesTest, OnCommittedReadbackIsExactlyTheInsertedRowsById) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    config.partition_interval = GetParam();
    LogStore store{config};
    store.Open();
    store.Initialize();
    ASSERT_EQ(store.Insert({Log("2024-01-01T00:00:00Z", "old")}), 1);

    std::vector<nlohmann::json> logs{Log("2024-01-01T00:20:00Z", "newer"),
                                     {{"timestamp", "2024-01-01T00:30:00Z"}},
                                     Log("2024-01-01T00:10:00Z", "late")};
    int calls = 0;
    EXPECT_EQ(store.Insert(
                  logs, {},
                  [&](const WriterDatabase& file, int count) {
                      ++calls;
                      const auto fields = ResolveLogFields(file.catalog()->log_column_info, {"*"});
                      const auto rows =
                          file.ReadLogs(fields, {}, Database::LogOrder::kIdDescending, count, 0);
                      EXPECT_EQ(Messages(rows), (std::vector<std::string>{"late", "newer"}));
                  }),
              2);
    EXPECT_EQ(calls, 1);
}

TEST_P(LiveStorageModesTest, InvalidRowsAreAcknowledgedWithoutOnCommitted) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    config.partition_interval = GetParam();
    LogStore store{config};
    store.Open();
    store.Initialize();
    std::vector<nlohmann::json> invalid{{{"timestamp", "2024-01-01T00:00:00Z"}}};
    size_t prefix = 0;
    int calls = 0;
    EXPECT_EQ(store.Insert(
                  invalid, [&](size_t value) { prefix = value; },
                  [&](const WriterDatabase&, int) { ++calls; }),
              0);
    EXPECT_EQ(prefix, 1u);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(store.GetCommittedLogId(), 0);
}

TEST_P(LiveStorageModesTest, ObserverFailureCannotRestoreCommittedPrefixToBacklog) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    config.partition_interval = GetParam();
    LogStore store{config};
    store.Open();
    store.Initialize();
    Backlog backlog{10};
    backlog.Add(Log("2024-01-01T00:00:00Z", "first"));
    backlog.Add(Log("2024-01-02T00:00:00Z", "second"));
    int calls = 0;
    EXPECT_THROW(backlog.FlushCommitted([&](auto& entries, const auto& acknowledge) {
        return store.Insert(entries, acknowledge, [&](const WriterDatabase&, int) {
            ++calls;
            throw std::runtime_error("injected publication failure");
        });
    }),
                 std::runtime_error);
    const int committed = GetParam() == PartitionInterval::kNone ? 2 : 1;
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(store.EstimateLogRowCount(), committed);
    EXPECT_EQ(store.GetCommittedLogId(), committed);
    EXPECT_EQ(backlog.Size(), 2u - committed);
    EXPECT_EQ(backlog.FlushCommitted([&](auto& entries, const auto& acknowledge) {
        return store.Insert(entries, acknowledge);
    }),
              2 - committed);
    EXPECT_EQ(store.EstimateLogRowCount(), 2);
}

INSTANTIATE_TEST_SUITE_P(StorageModes, LiveStorageModesTest,
                         ::testing::Values(PartitionInterval::kNone, PartitionInterval::kHourly,
                                           PartitionInterval::kDaily));
