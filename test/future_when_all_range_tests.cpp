/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <array>
#include <atomic>
#include <chrono> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/ready_future.hpp>
#include <stlab/concurrency/serial_queue.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/test/model.hpp>
#include <stlab/utility.hpp>

#include "future_test_helper.hpp"

using namespace stlab;
using namespace future_test_helper;

namespace {

/// Selects a public future-join overload.
enum class join_form : std::uint8_t { all_arguments, any_arguments, all_range, any_range };

/// Joins two consumed inputs using the selected overload and callable.
template <class E, class T, class F>
auto join_for_test(join_form form, E executor, future<T> a, future<T> b, F f) {
    if (form == join_form::all_arguments)
        return when_all(executor, std::move(f), std::move(a), std::move(b));
    if (form == join_form::any_arguments)
        return when_any(executor, std::move(f), std::move(a), std::move(b));
    std::array inputs{std::move(a), std::move(b)};
    auto range =
        std::pair{std::make_move_iterator(inputs.begin()), std::make_move_iterator(inputs.end())};
    if (form == join_form::all_range) return when_all(executor, std::move(f), range);
    return when_any(executor, std::move(f), range);
}

/// A copyable value whose copies report a requested extraction failure.
struct throwing_join_value {
    bool* _throw;

    /// Borrows the flag controlling copy failure.
    explicit throwing_join_value(bool& should_throw) : _throw(&should_throw) {}

    /// Copies the value, reporting a requested failure.
    throwing_join_value(const throwing_join_value& x) : _throw(x._throw) {
        if (*_throw) throw test_exception("join extraction");
    }

    /// Transfers the value.
    throwing_join_value(throwing_join_value&&) noexcept = default;
    /// Copies the value's flag.
    auto operator=(const throwing_join_value&) -> throwing_join_value& = default;
    /// Transfers the value's flag.
    auto operator=(throwing_join_value&&) noexcept -> throwing_join_value& = default;
};

} // namespace

TEST_CASE_TEMPLATE(
    "future_join_discarded_task_breaks_promise", T, int, void, std::unique_ptr<int>) {
    bool deferred = false;
    SUBCASE("discard immediately") {}
    SUBCASE("discard after submission") { deferred = true; }

    for (auto form : {join_form::all_arguments, join_form::any_arguments, join_form::all_range,
                      join_form::any_range}) {
        CAPTURE(static_cast<int>(form));
        std::vector<task<void() noexcept>> queue;
        auto executor = [&](task<void() noexcept> work) {
            if (deferred) queue.push_back(std::move(work));
        };
        auto produce = []() -> T {
            if constexpr (std::is_same_v<T, int>) return 7;
            if constexpr (std::is_same_v<T, std::unique_ptr<int>>) return std::make_unique<int>(7);
        };
        auto [p0, f0] = package<T()>(immediate_executor, produce);
        auto [p1, f1] = package<T()>(immediate_executor, produce);
        bool called = false;
        auto result = join_for_test(form, executor, std::move(f0), std::move(f1), [&](auto&&...) {
            called = true;
            return 42;
        });
        p0();
        p1();
        if (deferred) REQUIRE_FALSE(result.is_ready());
        queue.clear();
        CHECK_FALSE(called);
        CHECK(result.is_ready());
        if (!result.is_ready()) continue;
        REQUIRE(result.exception());
        try {
            std::rethrow_exception(result.exception());
        } catch (const future_error& error) {
            CHECK(error.code() == future_error_codes::broken_promise);
        }
    }
}

TEST_CASE_TEMPLATE("future_join_abandoned_inputs_report_broken_promise", T, int, void) {
    for (auto form : {join_form::all_arguments, join_form::any_arguments, join_form::all_range,
                      join_form::any_range}) {
        CAPTURE(static_cast<int>(form));
        auto produce = []() -> T {
            if constexpr (!std::is_void_v<T>) return 7;
        };
        auto [p0, f0] = package<T()>(immediate_executor, produce);
        auto [p1, f1] = package<T()>(immediate_executor, produce);
        auto result = join_for_test(form, immediate_executor, std::move(f0), std::move(f1),
                                    [](auto&&...) { return 42; });
        p0 = {};
        p1 = {};
        REQUIRE(result.is_ready());
        REQUIRE(result.exception());
        try {
            std::rethrow_exception(result.exception());
        } catch (const future_error& error) {
            CHECK(error.code() == future_error_codes::broken_promise);
        }
    }
}

TEST_CASE("future_join_failures_preserve_all_and_any_selection_rules") {
    bool all_fail = false;
    SUBCASE("a later success wins") {}
    SUBCASE("all fail with the last error") { all_fail = true; }
    for (auto form : {join_form::all_arguments, join_form::any_arguments, join_form::all_range,
                      join_form::any_range}) {
        CAPTURE(static_cast<int>(form));
        const bool any = form == join_form::any_arguments || form == join_form::any_range;
        auto [p0, f0] = package<int()>(immediate_executor, [] { return 7; });
        auto [p1, f1] = package<int()>(immediate_executor, [] { return 8; });
        auto result = join_for_test(form, immediate_executor, std::move(f0), std::move(f1),
                                    [](auto&&...) { return 42; });
        const auto first = std::make_exception_ptr(test_exception("first"));
        p0.set_exception(first);
        if (any) {
            REQUIRE_FALSE(result.is_ready());
            if (!all_fail) {
                p1();
                REQUIRE(result.is_ready());
                CHECK(result.get_ready() == 42);
            } else {
                const auto last = std::make_exception_ptr(test_exception("last"));
                p1.set_exception(last);
                REQUIRE(result.is_ready());
                check_failure<test_exception>(result, "last");
            }
        } else {
            REQUIRE(result.is_ready());
            check_failure<test_exception>(result, "first");
            CHECK(p1.canceled());
        }
    }
}

TEST_CASE("future_join_cancellation_releases_only_its_own_input_interest") {
    for (auto form : {join_form::all_arguments, join_form::any_arguments, join_form::all_range,
                      join_form::any_range}) {
        CAPTURE(static_cast<int>(form));
        auto [p0, f0] = package<int()>(immediate_executor, [] { return 7; });
        auto [p1, f1] = package<int()>(immediate_executor, [] { return 8; });
        auto retained = f0;
        auto result = join_for_test(form, immediate_executor, std::move(f0), std::move(f1),
                                    [](auto&&...) { return 42; });
        result.reset();
        CHECK_FALSE(p0.canceled());
        CHECK(p1.canceled());
        p0();
        CHECK(retained.get_ready() == 7);
    }
}

TEST_CASE("future_join_extraction_failure_completes_the_aggregate") {
    for (auto form : {join_form::all_arguments, join_form::any_arguments, join_form::all_range,
                      join_form::any_range}) {
        CAPTURE(static_cast<int>(form));
        bool should_throw = false;
        auto produce = [&] { return throwing_join_value(should_throw); };
        auto [p0, f0] = package<throwing_join_value()>(immediate_executor, produce);
        auto [p1, f1] = package<throwing_join_value()>(immediate_executor, produce);
        std::vector<task<void() noexcept>> queue;
        auto result = join_for_test(
            form, [&](task<void() noexcept> work) { queue.push_back(std::move(work)); }, f0,
            std::move(f1), [](auto&&...) { return 42; });
        p0();
        p1();
        should_throw = true;
        REQUIRE(queue.size() == 1);
        queue.back()();
        REQUIRE(result.is_ready());
        check_failure<test_exception>(result, "join extraction");
    }
}

TEST_CASE("future_join_any_selection_precedes_final_executor_dispatch") {
    bool range = false;
    SUBCASE("arguments") {}
    SUBCASE("range") { range = true; }
    auto [p0, f0] = package<int()>(immediate_executor, [] { return 7; });
    auto [p1, f1] = package<int()>(immediate_executor, [] { return 8; });
    std::vector<task<void() noexcept>> queue;
    auto executor = [&](task<void() noexcept> work) { queue.push_back(std::move(work)); };
    auto result = [&, first = std::move(f0), second = std::move(f1)]() mutable {
        if (!range) return when_any(executor, when_any_result, std::move(first), std::move(second));
        std::array inputs{std::move(first), std::move(second)};
        return when_any(executor, when_any_result, std::pair{inputs.begin(), inputs.end()});
    }();
    p1();
    p0();
    REQUIRE_FALSE(result.is_ready());
    REQUIRE(queue.size() == 1);
    queue.back()();
    REQUIRE(result.is_ready());
    CHECK((result.get_ready() == std::pair{8, std::size_t{1}}));
}

TEST_CASE("future_join_concurrent_completion_during_registration") {
    for (auto form : {join_form::all_arguments, join_form::any_arguments, join_form::all_range,
                      join_form::any_range}) {
        CAPTURE(static_cast<int>(form));
        for (int repeat = 0; repeat != 32; ++repeat) {
            auto [p0, f0] = package<int()>(immediate_executor, [] { return 7; });
            auto [p1, f1] = package<int()>(immediate_executor, [] { return 8; });
            std::thread a([p = std::move(p0)]() mutable { p(); });
            std::thread b([p = std::move(p1)]() mutable { p(); });
            auto result = join_for_test(form, immediate_executor, std::move(f0), std::move(f1),
                                        [](auto&&...) { return 42; });
            a.join();
            b.join();
            REQUIRE(result.is_ready());
            CHECK(result.get_ready() == 42);
        }
    }
}

TEST_CASE("future_join_ready_inputs_submit_user_work_without_running_inline") {
    for (auto form : {join_form::all_arguments, join_form::any_arguments, join_form::all_range,
                      join_form::any_range}) {
        CAPTURE(static_cast<int>(form));
        std::vector<task<void() noexcept>> queue;
        bool executing = false;
        auto result = join_for_test(
            form, [&](task<void() noexcept> work) { queue.push_back(std::move(work)); },
            make_ready_future<int>(7, immediate_executor),
            make_ready_future<int>(8, immediate_executor),
            [&](auto&&...) {
                CHECK(executing);
                return 42;
            });
        REQUIRE_FALSE(result.is_ready());
        REQUIRE(queue.size() == 1);
        executing = true;
        auto work = std::move(queue.back());
        queue.pop_back();
        work();
        REQUIRE(result.is_ready());
        CHECK(result.get_ready() == 42);
    }
}

TEST_CASE("future_join_cancellation_before_queued_dispatch_does_not_invoke_user_work") {
    for (auto form : {join_form::all_arguments, join_form::any_arguments, join_form::all_range,
                      join_form::any_range}) {
        CAPTURE(static_cast<int>(form));
        std::vector<task<void() noexcept>> queue;
        bool called = false;
        auto result = join_for_test(
            form, [&](task<void() noexcept> work) { queue.push_back(std::move(work)); },
            make_ready_future<int>(7, immediate_executor),
            make_ready_future<int>(8, immediate_executor),
            [&](auto&&...) {
                called = true;
                return 42;
            });
        REQUIRE(queue.size() == 1);
        result.reset();
        queue.back()();
        CHECK_FALSE(called);
    }
}

TEST_CASE("future_when_all_range_consumed_ready_copyable_values_are_not_copied") {
    annotate_counters counters;
    using value_t = std::optional<annotate>;
    auto [p0, f0] =
        package<value_t()>(immediate_executor, [&] { return value_t{std::in_place, counters}; });
    std::array futures{std::move(f0)};
    p0();

    auto result = when_all(
        immediate_executor, [](std::vector<value_t> values) { return values; },
        std::pair{std::make_move_iterator(futures.begin()),
                  std::make_move_iterator(futures.end())});
    REQUIRE(!futures[0].valid());
    REQUIRE(result.is_ready());

    auto values = await(std::move(result));
    REQUIRE(values.size() == 1);
    REQUIRE(values[0].has_value());
    REQUIRE(counters._copy_ctor.load() == 0);
    REQUIRE(counters._copy_assign_rhs.load() == 0);
}

TEST_CASE("future_when_all_range_preserves_shared_copyable_values") {
    std::array futures{make_ready_future<std::string>("first", immediate_executor),
                       make_ready_future<std::string>("second", immediate_executor)};

    auto result = when_all(
        immediate_executor, [](std::vector<std::string> values) { return values; },
        std::pair{futures.begin(), futures.end()});

    REQUIRE((await(std::move(result)) == std::vector<std::string>{"first", "second"}));
    REQUIRE(futures[0].get_ready() == "first");
    REQUIRE(futures[1].get_ready() == "second");
}

TEST_CASE("future_when_all_range_transfers_move_only_values_in_input_order") {
    auto [p0, f0] = package<std::unique_ptr<int>()>(immediate_executor,
                                                    [] { return std::make_unique<int>(42); });
    auto [p1, f1] = package<std::unique_ptr<int>()>(immediate_executor,
                                                    [] { return std::make_unique<int>(7); });
    std::array futures{std::move(f0), std::move(f1)};

    auto result = when_all(
        immediate_executor,
        [](std::vector<std::unique_ptr<int>> values) {
            REQUIRE(values.size() == 2);
            return std::array{std::move(values[0]), std::move(values[1])};
        },
        std::pair{futures.begin(), futures.end()});

    REQUIRE(!futures[0].valid());
    REQUIRE(!futures[1].valid());
    p1();
    REQUIRE(!result.is_ready());
    p0();

    auto values = await(std::move(result));
    REQUIRE(values.size() == 2);
    REQUIRE(values[0]);
    REQUIRE(values[1]);
    REQUIRE(*values[0] == 42);
    REQUIRE(*values[1] == 7);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_void_empty_range") {
    bool check = {false};
    std::vector<stlab::future<void>> emptyFutures;

    sut = when_all(
        make_executor<0>(), [&_check = check]() { _check = true; },
        std::make_pair(emptyFutures.begin(), emptyFutures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE(check);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_empty_range") {
    size_t p = 0;
    std::vector<stlab::future<int>> emptyFutures;

    sut = when_all(
        make_executor<0>(), [&_p = p](const std::vector<int>& v) { _p = v.size(); },
        std::make_pair(emptyFutures.begin(), emptyFutures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE((p == size_t(0)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_one_element") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), [] { return 42; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](std::vector<int> v) {
            _p = v.size();
            _r = v[0];
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE((p == size_t(1)));
    REQUIRE((r == size_t(42)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_all_delayed") {
    using namespace std::chrono_literals;

    stlab::serial_queue_t const seriel_queue_1{stlab::default_executor};
    stlab::serial_queue_t const seriel_queue_2{stlab::default_executor};
    stlab::serial_queue_t const seriel_queue_3{stlab::default_executor};

    std::vector<stlab::future<void>> test_futures;
    test_futures.emplace_back(stlab::async(seriel_queue_1.executor(), [] {
        invoke_waiting([] { std::this_thread::sleep_for(0.1s); });
    }));

    test_futures.emplace_back(stlab::async(seriel_queue_2.executor(), [] {
        invoke_waiting([] { std::this_thread::sleep_for(0.1s); });
    }));

    test_futures.emplace_back(stlab::async(seriel_queue_3.executor(), [] {
        invoke_waiting([] { std::this_thread::sleep_for(0.1s); });
    }));
    bool done{false};
    auto done_future = stlab::when_all(
        stlab::default_executor, [&done] { done = true; },
        std::make_pair(test_futures.begin(), test_futures.end()));

    stlab::await(std::move(done_future));

    REQUIRE(done);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_many_elements") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), [] { return 1; }));
    futures.push_back(async(make_executor<0>(), [] { return 2; }));
    futures.push_back(async(make_executor<0>(), [] { return 3; }));
    futures.push_back(async(make_executor<0>(), [] { return 5; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE((p == size_t(4)));
    REQUIRE((r == size_t(1 + 2 + 3 + 5)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_when_all_void_range_with_many_elements_and_immediate_continuation") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.reserve(1000);
    for (auto i = 0; i < 1000; ++i) {
        futures.push_back(async(make_executor<0>(), [] { return 1; }));
    }

    sut = when_all(
        immediate_executor,
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE((p == size_t(1000)));
    REQUIRE((r == size_t(1000)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1000);
}

/*
       /  F1  \
      / / F2 \ \
start           sut
      \ \ F3 / /
       \  F4  /
*/
TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_when_all_void_range_with_diamond_formation_elements") {
    std::array v{0, 0, 0, 0};
    int r = 0;
    auto start = async(make_executor<0>(), [] { return 4711; });
    std::vector<stlab::future<void>> futures(4);
    futures[0] = start.then(make_executor<0>(), [&_p = v[0]](auto x) { _p = x + 1; });
    futures[1] = start.then(make_executor<0>(), [&_p = v[1]](auto x) { _p = x + 2; });
    futures[2] = start.then(make_executor<0>(), [&_p = v[2]](auto x) { _p = x + 3; });
    futures[3] = start.then(make_executor<0>(), [&_p = v[3]](auto x) { _p = x + 5; });

    sut = when_all(
        make_executor<1>(),
        [&_r = r, &v]() {
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE(r == 4711 + 1 + 4711 + 2 + 4711 + 3 + 4711 + 5);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 5);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_int_empty_range") {
    std::vector<stlab::future<int>> emptyFutures;

    sut = when_all(
        make_executor<0>(), [](const std::vector<int>& v) { return static_cast<int>(v.size()); },
        std::make_pair(emptyFutures.begin(), emptyFutures.end()));
    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((*sut.get_try() == 0));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_int_range_with_one_element") {
    size_t p = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), [] { return 42; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p](std::vector<int> v) {
            _p = v.size();
            return v[0];
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((p == size_t(1)));
    REQUIRE((*sut.get_try() == 42));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_int_range_with_many_elements") {
    size_t p = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), [] { return 1; }));
    futures.push_back(async(make_executor<0>(), [] { return 2; }));
    futures.push_back(async(make_executor<0>(), [] { return 3; }));
    futures.push_back(async(make_executor<0>(), [] { return 5; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p](const std::vector<int>& v) {
            _p = v.size();
            auto r = 0;
            for (auto i : v) {
                r += i;
            }
            return r;
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((p == size_t(4)));
    REQUIRE((*sut.get_try() == 1 + 2 + 3 + 5));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

/*
       /  F1  \
      / / F2 \ \
start           sut
      \ \ F3 / /
       \  F4  /
*/
TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_int_range_with_diamond_formation_elements") {
    size_t p = 0;
    auto start = async(make_executor<0>(), [] { return 4711; });
    std::vector<stlab::future<int>> futures(4);
    futures[0] = start.then(make_executor<0>(), [](auto x) { return x + 1; });
    futures[1] = start.then(make_executor<0>(), [](auto x) { return x + 2; });
    futures[2] = start.then(make_executor<0>(), [](auto x) { return x + 3; });
    futures[3] = start.then(make_executor<0>(), [](auto x) { return x + 5; });

    sut = when_all(
        make_executor<1>(),
        [&_p = p](const std::vector<int>& v) {
            _p = v.size();
            auto r = 0;
            for (auto i : v) {
                r += i;
            }
            return r;
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((p == size_t(4)));
    REQUIRE((*sut.get_try() == 4711 + 1 + 4711 + 2 + 4711 + 3 + 4711 + 5));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>, "future_when_all_move_range_with_many_elements") {
    size_t p = 0;
    std::vector<stlab::future<stlab::move_only>> futures;
    futures.push_back(async(make_executor<0>(), [] { return stlab::move_only{1}; }));
    futures.push_back(async(make_executor<0>(), [] { return stlab::move_only{2}; }));
    futures.push_back(async(make_executor<0>(), [] { return stlab::move_only{3}; }));
    futures.push_back(async(make_executor<0>(), [] { return stlab::move_only{5}; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p](const std::vector<stlab::move_only>& v) {
            _p = v.size();
            auto r = 0;
            for (const auto& i : v) {
                r += i.member();
            }
            return stlab::move_only{r};
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE((p == size_t(4)));
    REQUIRE(result.member() == 1 + 2 + 3 + 5);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_range_with_mutable_task") {
    struct mutable_int {
        int i = 0;
        auto operator()() {
            ++i;
            return i;
        }
    };
    mutable_int func1;
    mutable_int func2;
    std::vector<future<mutable_int>> futures{
        async(stlab::default_executor,
              [func = func1]() mutable {
                  func();
                  return func;
              }),
        async(stlab::default_executor, [func = func2]() mutable {
            func();
            return func;
        })};
    auto result = when_all(
        stlab::default_executor,
        [](auto result_vec) {
            return std::accumulate(result_vec.begin(), result_vec.end(), 0,
                                   [](int sum, auto f) { return sum + f(); });
        },
        std::make_pair(futures.begin(), futures.end()));

    REQUIRE(stlab::await(std::move(result)) == 4);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_range_with_mutable_void_task") {
    std::atomic_int check{0};
    struct mutable_int {
        std::atomic_int& _check;
        int i = 0;
        void operator()() {
            ++i;
            ++_check;
        }
    };

    mutable_int func1{check};
    mutable_int func2{check};
    std::vector<future<void>> futures{
        async(stlab::default_executor, [func = func1]() mutable { func(); }),
        async(stlab::default_executor, [func = func2]() mutable { func(); })};

    future<void> result = when_all(
        stlab::default_executor, [_func = mutable_int{check}]() mutable { _func(); },
        std::make_pair(futures.begin(), futures.end()));

    stlab::await(std::move(result));

    REQUIRE(check == 3);
}

// ----------------------------------------------------------------------------
//                             Error cases
// ----------------------------------------------------------------------------

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_one_element") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            _r = v[0];
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE((p == size_t(0)));
    REQUIRE((r == size_t(0)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_many_elements_one_failing") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));
    futures.push_back(async(make_executor<0>(), [] { return 2; }));
    futures.push_back(async(make_executor<0>(), [] { return 3; }));
    futures.push_back(async(make_executor<0>(), [] { return 5; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE((p == size_t(0)));
    REQUIRE((r == size_t(0)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_many_elements_all_failing") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE((p == size_t(0)));
    REQUIRE((r == size_t(0)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

/*
       /  F1  \
      / / F2 \ \
start           sut
      \ \ F3 / /
       \  F4  /
*/
TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_when_all_void_range_with_diamond_formation_elements_start_failing") {
    std::array v{0, 0, 0, 0};
    int r = 0;
    auto start = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    std::vector<stlab::future<void>> futures(4);
    futures[0] = start.then(make_executor<0>(), [&_p = v[0]](auto x) { _p = x + 1; });
    futures[1] = start.then(make_executor<0>(), [&_p = v[1]](auto x) { _p = x + 2; });
    futures[2] = start.then(make_executor<0>(), [&_p = v[2]](auto x) { _p = x + 3; });
    futures[3] = start.then(make_executor<0>(), [&_p = v[3]](auto x) { _p = x + 5; });

    sut = when_all(
        make_executor<1>(),
        [&_r = r, &v]() {
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(r == 0);
    for (auto d : v) {
        REQUIRE(d == 0);
    }
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(
    test_fixture<void>,
    "future_when_all_void_range_with_diamond_formation_elements_one_parallel_failing") {
    std::array v{0, 0, 0, 0};
    int r = 0;
    auto start = async(make_executor<0>(), []() -> int { return 42; });
    std::vector<stlab::future<void>> futures(4);
    futures[0] = start.then(make_executor<0>(), [&_p = v[0]](auto x) { _p = x + 1; });
    futures[1] = start.then(make_executor<0>(), [](auto) { throw test_exception("failure"); });
    futures[2] = start.then(make_executor<0>(), [&_p = v[2]](auto x) { _p = x + 3; });
    futures[3] = start.then(make_executor<0>(), [&_p = v[3]](auto x) { _p = x + 5; });

    sut = when_all(
        make_executor<1>(),
        [&_r = r, &v]() {
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(r == 0);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_when_all_void_range_with_diamond_formation_elements_join_failing") {
    std::array v{0, 0, 0, 0};
    int const r = 0;
    auto start = async(make_executor<0>(), []() -> int { return 42; });
    std::vector<stlab::future<void>> futures(4);
    futures[0] = start.then(make_executor<0>(), [&_p = v[0]](auto x) { _p = x + 1; });
    futures[1] = start.then(make_executor<0>(), [&_p = v[1]](auto x) { _p = x + 2; });
    futures[2] = start.then(make_executor<0>(), [&_p = v[2]](auto x) { _p = x + 3; });
    futures[3] = start.then(make_executor<0>(), [&_p = v[3]](auto x) { _p = x + 5; });

    sut = when_all(
        make_executor<1>(), []() { throw test_exception("failure"); },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(r == 0);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(
    test_fixture<void>,
    "future_when_all_void_range_with_one_fails_ofmany_elements_and_immediate_continuation") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.reserve(500);
    for (auto i = 0; i < 500; ++i) {
        futures.push_back(async(make_executor<0>(), [] { return 1; }));
    }
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));

    for (auto i = 0; i < 499; ++i) {
        futures.push_back(async(make_executor<0>(), [] { return 1; }));
    }

    sut = when_all(
        immediate_executor,
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(r == 0u);
}
