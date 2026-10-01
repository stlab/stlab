
#include <stlab/concurrency/system_timer.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <ratio>
#include <stdexcept>
#include <thread>
#include <utility>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>

/**************************************************************************************************/

using namespace stlab;

/**************************************************************************************************/

TEST_CASE("system_timer_cancellation") {
    // Test to ensure that a task scheduled after pre_exit() is not executed
    system_timer(std::chrono::hours(1), []() noexcept { FAIL(""); });

    auto [task, future] = package<int()>(immediate_executor, [] { return 42; });
    system_timer(std::chrono::seconds(0), std::move(task));

    REQUIRE(await(std::move(future)) == 42);
}

TEST_CASE("system_timer supports steady deadlines without deprecation") {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(error : 4996)
#elif defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wdeprecated-declarations"
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    std::promise<std::chrono::steady_clock::time_point> completion;
    auto result = completion.get_future();
    system_timer(deadline,
                 [&]() noexcept { completion.set_value(std::chrono::steady_clock::now()); });
    REQUIRE(result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(result.get() >= deadline);

    const auto caller = std::this_thread::get_id();
    std::promise<std::thread::id> past;
    auto past_result = past.get_future();
    system_timer(std::chrono::steady_clock::time_point::min(),
                 [&]() noexcept { past.set_value(std::this_thread::get_id()); });
    REQUIRE(past_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(past_result.get() != caller);
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

TEST_CASE("system_timer accepts maximum signed nanosecond delay") {
    CHECK_NOTHROW(system_timer(std::chrono::nanoseconds::max(), []() noexcept {}));
}

TEST_CASE("system_timer nonpositive delays never invoke inline") {
    const auto caller = std::this_thread::get_id();
    for (auto delay :
         {std::chrono::hours::zero(), std::chrono::hours(-1), std::chrono::hours::min()}) {
        std::promise<std::thread::id> completion;
        auto result = completion.get_future();
        system_timer(delay, [&]() noexcept { completion.set_value(std::this_thread::get_id()); });
        REQUIRE(result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        CHECK(result.get() != caller);
    }
}

TEST_CASE("system_timer accepts extreme rational periods without overflowing conversion") {
    using extreme_period = std::ratio<std::numeric_limits<std::intmax_t>::max()>;
    std::promise<void> completion;
    auto result = completion.get_future();
    system_timer(std::chrono::duration<int, extreme_period>(-1),
                 [&]() noexcept { completion.set_value(); });
    REQUIRE(result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    task<void() noexcept> target = []() noexcept {};
    CHECK_THROWS_AS(system_timer(std::chrono::duration<int, extreme_period>(1), std::move(target)),
                    std::overflow_error);

    using fractional_period = std::ratio<std::numeric_limits<std::intmax_t>::max(),
                                         std::numeric_limits<std::intmax_t>::max() - 2>;
    std::promise<void> fractional;
    auto fractional_result = fractional.get_future();
    const auto before = std::chrono::steady_clock::now();
    system_timer(std::chrono::duration<int, fractional_period>(1),
                 [&]() noexcept { fractional.set_value(); });
    REQUIRE(fractional_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(std::chrono::steady_clock::now() - before >= std::chrono::seconds(1));
}

TEST_CASE("system_timer rejects nonfinite and overflowing delays without consuming target") {
    bool retained = false;
    task<void() noexcept> target = [owned = std::make_unique<int>(42), &retained]() noexcept {
        retained = owned && *owned == 42;
    };
    CHECK_THROWS_AS(
        system_timer(std::chrono::duration<double>(std::numeric_limits<double>::quiet_NaN()),
                     std::move(target)),
        std::invalid_argument);
    CHECK(target);
    CHECK_THROWS_AS(
        system_timer(std::chrono::duration<double>(std::numeric_limits<double>::infinity()),
                     std::move(target)),
        std::invalid_argument);
    CHECK(target);
    CHECK_THROWS_AS(system_timer(std::chrono::hours::max(), std::move(target)),
                    std::overflow_error);
    CHECK(target);
    target();
    CHECK(retained);
}

TEST_CASE("system_timer earlier insertion wakes a waiting timer") {
    std::promise<void> later;
    auto later_result = later.get_future();
    system_timer(std::chrono::milliseconds(500), [&]() noexcept { later.set_value(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::promise<void> earlier;
    auto earlier_result = earlier.get_future();
    system_timer(std::chrono::milliseconds(1), [&]() noexcept { earlier.set_value(); });
    CHECK(earlier_result.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready);
    REQUIRE(later_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(earlier_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
}

TEST_CASE("system_timer executes and releases a move-only capture exactly once") {
    std::atomic<int> calls{0};
    std::atomic<int> destructions{0};
    auto deleter = [&](int* p) {
        ++destructions;
        delete p;
    };
    std::promise<void> completion;
    auto result = completion.get_future();
    system_timer(std::chrono::duration<double, std::nano>(0.25),
                 [owned = std::unique_ptr<int, decltype(deleter)>(new int(42), deleter), &calls,
                  &completion]() noexcept {
                     if (*owned == 42) ++calls;
                     completion.set_value();
                 });
    REQUIRE(result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!destructions && std::chrono::steady_clock::now() < limit)
        std::this_thread::yield();
    CHECK(calls == 1);
    CHECK(destructions == 1);
}

/**************************************************************************************************/
