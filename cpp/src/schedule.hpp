#ifndef LOGLITE_SCHEDULE_HPP_
#define LOGLITE_SCHEDULE_HPP_

#include <boost/asio.hpp>

#include <type_traits>
#include <utility>

namespace loglite {

// Application services schedule work. File-local databases stay synchronous.
template <typename F>
auto Schedule(boost::asio::any_io_executor executor, F&& function)
    -> boost::asio::awaitable<std::invoke_result_t<std::decay_t<F>>> {
    using Result = std::invoke_result_t<std::decay_t<F>>;
    auto task = std::forward<F>(function);
    if constexpr (std::is_void_v<Result>) {
        co_await boost::asio::co_spawn(
            std::move(executor),
            [task = std::move(task)]() mutable -> boost::asio::awaitable<void> {
                task();
                co_return;
            },
            boost::asio::use_awaitable);
        co_return;
    } else {
        auto result = co_await boost::asio::co_spawn(
            std::move(executor),
            [task = std::move(task)]() mutable -> boost::asio::awaitable<Result> {
                co_return task();
            },
            boost::asio::use_awaitable);
        co_return std::move(result);
    }
}

}  // namespace loglite

#endif  // LOGLITE_SCHEDULE_HPP_
