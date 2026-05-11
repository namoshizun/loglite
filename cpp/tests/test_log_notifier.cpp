#include <gtest/gtest.h>

#include "globals.hpp"

#include <boost/asio.hpp>
#include <thread>

namespace asio = boost::asio;
using namespace loglite;

class LogNotifierTest : public ::testing::Test {
   protected:
    void SetUp() override { ioc_ = std::make_unique<asio::io_context>(); }
    void TearDown() override {
        ioc_->stop();
        ioc_.reset();
    }
    std::unique_ptr<asio::io_context> ioc_;
};

TEST_F(LogNotifierTest, SubscribeIncreasesCount) {
    LogNotifier notifier;
    EXPECT_EQ(notifier.SubscriberCount(), 0u);

    auto sub = notifier.Subscribe(ioc_->get_executor());
    EXPECT_EQ(notifier.SubscriberCount(), 1u);

    auto sub2 = notifier.Subscribe(ioc_->get_executor());
    EXPECT_EQ(notifier.SubscriberCount(), 2u);
}

TEST_F(LogNotifierTest, UnsubscribeDecreasesCount) {
    LogNotifier notifier;
    auto sub = notifier.Subscribe(ioc_->get_executor());
    auto sub2 = notifier.Subscribe(ioc_->get_executor());
    EXPECT_EQ(notifier.SubscriberCount(), 2u);

    notifier.Unsubscribe(sub);
    EXPECT_EQ(notifier.SubscriberCount(), 1u);

    notifier.Unsubscribe(sub2);
    EXPECT_EQ(notifier.SubscriberCount(), 0u);
}

TEST_F(LogNotifierTest, GetLastIdInitiallyZero) {
    LogNotifier notifier;
    EXPECT_EQ(notifier.GetLastId(), 0);
}

TEST_F(LogNotifierTest, NotifyUpdatesLastId) {
    LogNotifier notifier;
    notifier.Notify(42);
    EXPECT_EQ(notifier.GetLastId(), 42);

    notifier.Notify(100);
    EXPECT_EQ(notifier.GetLastId(), 100);
}

TEST_F(LogNotifierTest, NotifyWithoutSubscribersDoesNotCrash) {
    LogNotifier notifier;
    EXPECT_NO_THROW(notifier.Notify(1));
    EXPECT_EQ(notifier.GetLastId(), 1);
}

TEST_F(LogNotifierTest, NotifyCancelsSubscriberTimers) {
    LogNotifier notifier;
    auto sub = notifier.Subscribe(ioc_->get_executor());

    // Start a non-expiring timer on the subscription
    sub->timer->expires_after(std::chrono::hours(1));
    sub->timer->async_wait([](const boost::system::error_code&) {});

    // Notify should cancel the timer
    notifier.Notify(1);

    // Run io_context to process cancellation
    ioc_->run();

    // Timer should be cancelled
    EXPECT_NE(sub->timer->expiry(), asio::steady_timer::time_point{});
}

TEST_F(LogNotifierTest, MultipleSubscriptions) {
    LogNotifier notifier;
    std::vector<std::shared_ptr<LogNotifier::Subscription>> subs;
    for (int i = 0; i < 5; ++i) {
        subs.push_back(notifier.Subscribe(ioc_->get_executor()));
    }
    EXPECT_EQ(notifier.SubscriberCount(), 5u);

    for (auto& s : subs) {
        s->timer->expires_after(std::chrono::hours(1));
        s->timer->async_wait([](const boost::system::error_code&) {});
    }

    notifier.Notify(99);
    ioc_->run();
    EXPECT_EQ(notifier.GetLastId(), 99);
}

TEST_F(LogNotifierTest, NotifyFromOtherThread) {
    LogNotifier notifier;
    auto sub = notifier.Subscribe(ioc_->get_executor());
    sub->timer->expires_after(std::chrono::hours(1));
    int completed = 0;
    sub->timer->async_wait([&](const boost::system::error_code& ec) {
        if (ec) ++completed;
    });

    std::thread t{[&]() { notifier.Notify(55); }};
    t.join();

    ioc_->run();
    EXPECT_EQ(notifier.GetLastId(), 55);
    EXPECT_EQ(completed, 1);
}
