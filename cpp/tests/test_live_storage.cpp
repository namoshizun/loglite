#include "ingestion.hpp"
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
    EXPECT_EQ(store.Insert(logs), 2);
    LogReaderPool readers{store, 1};
    const auto result = readers.UseConnection(
        [](LogReader& reader) { return reader.Query({"message"}, {}, 10, 0); });
    EXPECT_EQ(result.results,
              (std::vector<nlohmann::json>{
                  {{"message", "newer"}}, {{"message", "late"}}, {{"message", "old"}}}));
}

TEST_P(LiveStorageModesTest, InvalidRowsAreAcknowledgedWithoutOnCommitted) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    config.partition_interval = GetParam();
    LogStore store{config};
    store.Open();
    store.Initialize();
    EXPECT_EQ(store.Insert({{{"timestamp", "2024-01-01T00:00:00Z"}}}), 0);
    EXPECT_EQ(store.GetCommittedLogId(), 0);
    EXPECT_EQ(store.Insert({{{"timestamp", "2024-01-01T00:00:00Z"}}}), 0);
}

TEST_P(LiveStorageModesTest, ObserverFailureCannotRestoreCommittedPrefixToBacklog) {
    test::TempDirectory directory;
    auto config = test::MakeConfig(directory.path());
    config.partition_interval = GetParam();
    LogStore store{config};
    store.Open();
    store.Initialize();
    struct ThrowingFeed : LogNotifier {
        using LogNotifier::LogNotifier;
        void Publish(std::vector<nlohmann::json>) override {
            throw std::runtime_error("injected publication failure");
        }
    };
    ThrowingFeed feed{8};
    Ingestion ingestion{store, feed, 10, std::chrono::seconds{1}};
    ASSERT_TRUE(ingestion.Submit(Log("2024-01-01T00:00:00Z", "first")).admitted);
    ASSERT_TRUE(ingestion.Submit(Log("2024-01-02T00:00:00Z", "second")).admitted);
    EXPECT_EQ(ingestion.Settle(), 2);
    EXPECT_EQ(store.EstimateLogRowCount(), 2);
    EXPECT_EQ(ingestion.size(), 0u);
    EXPECT_EQ(ingestion.Settle(), 0);
    EXPECT_EQ(store.EstimateLogRowCount(), 2);
}

INSTANTIATE_TEST_SUITE_P(StorageModes, LiveStorageModesTest,
                         ::testing::Values(PartitionInterval::kNone, PartitionInterval::kHourly,
                                           PartitionInterval::kDaily));
