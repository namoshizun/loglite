#include <gtest/gtest.h>

#include "notifier.hpp"

#include <boost/asio.hpp>
#include <thread>

namespace asio = boost::asio;
using namespace loglite;
using namespace std::chrono_literals;

TEST(LogNotifierTest, SubscriptionLifecycleAndLastIdWorkWithoutActiveWaiters) {
    asio::io_context io;
    LogNotifier notifier;
    EXPECT_EQ(notifier.SubscriberCount(), 0u);
    EXPECT_EQ(notifier.GetLastId(), 0);
    const auto first = notifier.Subscribe(io.get_executor());
    const auto second = notifier.Subscribe(io.get_executor());
    EXPECT_EQ(notifier.SubscriberCount(), 2u);
    notifier.Unsubscribe(first);
    EXPECT_EQ(notifier.SubscriberCount(), 1u);
    notifier.Unsubscribe(second);
    EXPECT_EQ(notifier.SubscriberCount(), 0u);
    notifier.Notify(42);
    EXPECT_EQ(notifier.GetLastId(), 42);
    notifier.Notify(100);
    EXPECT_EQ(notifier.GetLastId(), 100);
}

TEST(LogNotifierTest, NotificationFromAnotherThreadCancelsEverySubscriberWait) {
    asio::io_context io;
    LogNotifier notifier;
    int cancelled = 0;
    for (int i = 0; i < 5; ++i) {
        auto subscription = notifier.Subscribe(io.get_executor());
        // Start a non-expiring timer on each subscription.
        subscription->timer->expires_after(1h);
        subscription->timer->async_wait([&](const boost::system::error_code& ec) {
            EXPECT_EQ(ec, asio::error::operation_aborted);
            ++cancelled;
        });
    }
    EXPECT_EQ(notifier.SubscriberCount(), 5u);
    {
        std::jthread notify{[&] { notifier.Notify(55); }};
    }
    // Bound the run: a missing cancellation should fail instead of waiting an hour.
    io.run_for(100ms);
    EXPECT_EQ(cancelled, 5);
    EXPECT_EQ(notifier.GetLastId(), 55);
}
