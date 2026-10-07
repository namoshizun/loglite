#include <gtest/gtest.h>

#include "notifier.hpp"

#include <boost/asio.hpp>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using namespace loglite;
using namespace std::chrono_literals;

namespace {

std::vector<int> Ids(const std::vector<LogNotifier::Record>& rows) {
    std::vector<int> ids;
    for (const auto& row : rows) ids.push_back((*row)["id"].get<int>());
    return ids;
}

}  // namespace

TEST(LogNotifierTest, RingIsBoundedAndKeepsNewestInCommitOrderWithoutSubscribers) {
    for (const size_t capacity : {1u, 2u, 50u}) {
        SCOPED_TRACE(capacity);
        LogNotifier notifier{capacity};
        std::vector<nlohmann::json> batch;
        for (int id = 1; id <= 75; ++id) batch.push_back({{"id", id}});
        notifier.Publish(std::move(batch));
        uint64_t cursor = 0;
        const auto rows = notifier.Since(cursor);
        ASSERT_EQ(rows.size(), capacity);
        for (size_t i = 0; i < capacity; ++i)
            EXPECT_EQ((*rows[i])["id"], 75 - static_cast<int>(capacity) + 1 + static_cast<int>(i));
        EXPECT_EQ(cursor, 75u);
        EXPECT_EQ(notifier.SubscriberCount(), 0u);
    }
}

TEST(LogNotifierTest, SinceReturnsOnlyRowsAfterCursorAndAdvancesIt) {
    LogNotifier notifier{10};
    notifier.Publish({{{"id", 1}}, {{"id", 2}}});
    uint64_t cursor = 0;
    EXPECT_EQ(Ids(notifier.Since(cursor)), (std::vector<int>{1, 2}));
    EXPECT_EQ(cursor, 2u);
    EXPECT_TRUE(notifier.Since(cursor).empty());
    EXPECT_EQ(cursor, 2u);
    notifier.Publish({{{"id", 3}}});
    EXPECT_EQ(Ids(notifier.Since(cursor)), (std::vector<int>{3}));
    EXPECT_EQ(cursor, 3u);
}

TEST(LogNotifierTest, SubscriberRegisteredAfterPublishReceivesNothingFromBefore) {
    asio::io_context io;
    LogNotifier notifier{10};
    notifier.Publish({{{"id", 1}}});
    auto timer = std::make_shared<asio::steady_timer>(io);
    uint64_t cursor = notifier.Subscribe(timer);
    EXPECT_EQ(notifier.SubscriberCount(), 1u);
    EXPECT_TRUE(notifier.Since(cursor).empty());
    notifier.Publish({{{"id", 2}}});
    EXPECT_EQ(Ids(notifier.Since(cursor)), (std::vector<int>{2}));
    notifier.Unsubscribe(timer);
    EXPECT_EQ(notifier.SubscriberCount(), 0u);
}

TEST(LogNotifierTest, SlowCursorSkipsEvictedRows) {
    LogNotifier notifier{2};
    uint64_t cursor = 0;
    notifier.Publish({{{"id", 1}}, {{"id", 2}}});
    notifier.Publish({{{"id", 3}}, {{"id", 4}}});
    EXPECT_EQ(Ids(notifier.Since(cursor)), (std::vector<int>{3, 4}));
    EXPECT_EQ(cursor, 4u);
}

TEST(LogNotifierTest, PublishFromAnotherThreadCancelsPendingTimerWait) {
    asio::io_context io;
    LogNotifier notifier{1};
    std::vector<std::shared_ptr<asio::steady_timer>> timers;
    int cancelled = 0;
    for (int i = 0; i < 5; ++i) {
        auto timer = std::make_shared<asio::steady_timer>(io);
        notifier.Subscribe(timer);
        timer->expires_after(1h);
        timer->async_wait([&](const boost::system::error_code& error) {
            EXPECT_EQ(error, asio::error::operation_aborted);
            ++cancelled;
        });
        timers.push_back(std::move(timer));
    }
    std::jthread publisher{[&] { notifier.Publish({{{"id", 1}}}); }};
    publisher.join();
    EXPECT_EQ(cancelled, 0);
    io.poll();
    EXPECT_EQ(cancelled, 5);
}
