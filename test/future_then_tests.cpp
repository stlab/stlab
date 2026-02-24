/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/executor_base.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/traits.hpp>
#include <stlab/test/model.hpp>
#include <stlab/utility.hpp>

#include "future_test_helper.hpp"

using namespace std;
using namespace stlab;
using namespace future_test_helper;

TEST_CASE_FIXTURE(test_fixture<void>, "future_void_single_task") {
    int p = 0;

    sut = async(make_executor<0>(), [&_p = p] { _p = 42; });

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE(p == 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_void_single_task_detached") {
    atomic_int p{0};
    {
        auto detached = async(make_executor<0>(), [&_p = p] { _p = 42; });
        detached.detach();
    }
    while (p.load() != 42) {
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_void_two_tasks_with_same_scheduler_then_on_rvalue") {
    /* because of a gcc version < 6 bug, it is not possible to use the following
    using task_t = function<void()>;
    using op_t = future<void>(future<void>::*)(task_t&&)&&;

    op_t ops[] = {static_cast<op_t>(&future<void>::then<task_t>),
                  static_cast<op_t>(&future<void>::operator|<task_t>)};

    for (const auto& op : ops)
    */
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }).then([&_p = p] { _p += 42; });

        check_valid_future(sut);
        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }) | [&_p = p] { _p += 42; };

        check_valid_future(sut);
        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_void_two_tasks_with_same_scheduler_then_on_lvalue") {
    {
        atomic_int p{0};
        auto interim = async(make_executor<0>(), [&_p = p] { _p = 42; });

        sut = interim.then([&_p = p] { _p += 42; });

        check_valid_future(sut);
        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
    {
        atomic_int p{0};
        auto interim = async(make_executor<0>(), [&_p = p] { _p = 42; });

        sut = interim | [&_p = p] { _p += 42; };

        check_valid_future(sut);
        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_int_void_two_tasks_with_same_scheduler") {
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [] { return 42; }).then([&_p = p](auto x) { _p = x + 42; });
        check_valid_future(sut);

        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [] { return 42; }) | [&_p = p](auto x) { _p = x + 42; };
        check_valid_future(sut);

        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_int_void_two_tasks_with_different_scheduler") {
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [] {
                  return 42;
              }).then(make_executor<1>(), [&_p = p](auto x) { _p = x + 42; });
        check_valid_future(sut);

        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [] { return 42; }) |
              (executor{make_executor<1>()} & [&_p = p](auto x) { _p = x + 42; });

        check_valid_future(sut);

        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_void_two_tasks_with_different_scheduler") {
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }).then(make_executor<1>(), [&_p = p] {
            _p += 42;
        });
        check_valid_future(sut);

        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }) |
              (executor{make_executor<1>()} & [&_p = p] { _p += 42; });

        check_valid_future(sut);

        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }
}

/*
        f1
       /
    sut
       \
        f2
*/
TEST_CASE_FIXTURE(test_fixture<void>, "future_void_Y_formation_tasks_with_same_scheduler") {
    {
        atomic_int p{0};
        int r1 = 0;
        int r2 = 0;

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; });
        auto f1 = sut.then([&_p = p, &_r = r1] { _r = 42 + _p; });
        auto f2 = sut.then([&_p = p, &_r = r2] { _r = 4711 + _p; });

        check_valid_future(sut, f1, f2);
        wait_until_future_completed(std::move(f1), std::move(f2));

        REQUIRE(r1 == 42 + 42);
        REQUIRE(r2 == 42 + 4711);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 3);
    }
    {
        atomic_int p{0};
        int r1 = 0;
        int r2 = 0;

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; });
        auto f1 = sut | [&_p = p, &_r = r1] { _r = 42 + _p; };
        auto f2 = sut | [&_p = p, &_r = r2] { _r = 4711 + _p; };

        check_valid_future(sut, f1, f2);
        wait_until_future_completed(std::move(f1), std::move(f2));

        REQUIRE(r1 == 42 + 42);
        REQUIRE(r2 == 42 + 4711);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 3);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "reduction_future_void") {
    {
        bool first{false};
        bool second{false};

        sut = async(make_executor<0>(), [&] { first = true; }).then([&] {
            return async(make_executor<0>(), [&] { second = true; });
        });

        wait_until_future_completed(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
    {
        bool first{false};
        bool second{false};

        sut = async(make_executor<0>(), [&] { first = true; }) |
              [&] { return async(make_executor<0>(), [&] { second = true; }); };

        wait_until_future_completed(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "reduction_future_int_to_void") {
    {
        atomic_bool first{false};
        atomic_bool second{false};
        atomic_int result{0};

        sut = async(default_executor, [&_flag = first] {
                  _flag = true;
                  return 42;
              }).then([&_flag = second, &_result = result](auto x) {
            return async(
                default_executor,
                [&_flag, &_result](auto x) {
                    _flag = true;
                    _result = x + 42;
                },
                x);
        });

        wait_until_future_completed(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(result == 84);
    }
    {
        atomic_bool first{false};
        atomic_bool second{false};
        atomic_int result{0};

        sut = async(default_executor,
                    [&_flag = first] {
                        _flag = true;
                        return 42;
                    }) |
              [&_flag = second, &_result = result](auto x) {
                  return async(
                      default_executor,
                      [&_flag, &_result](auto x) {
                          _flag = true;
                          _result = x + 42;
                      },
                      x);
              };

        wait_until_future_completed(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(result == 84);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "reduction_future_move_only_to_void") {
    {
        atomic_bool first{false};
        move_only result;

        sut = async(default_executor, [&_flag = first] {
                  _flag = true;
                  return move_only(42);
              }).then([&_result = result](auto&& x) {
            return async(
                default_executor, [&_result](auto&& x) { _result = std::forward<decltype(x)>(x); },
                std::forward<move_only>(x));
        });

        wait_until_future_completed(std::move(sut));

        REQUIRE(first);
        REQUIRE(result.member() == 42);
    }
    {
        bool first{false};
        move_only result;

        sut = async(immediate_executor, [&_flag = first] {
                  _flag = true;
                  return move_only(42);
              }).then([&_result = result](auto&& x) {
            return async(
                immediate_executor,
                [&_result](auto&& x) { _result = std::forward<decltype(x)>(x); },
                std::forward<move_only>(x));
        });

        REQUIRE(sut.get_try());

        REQUIRE(first);
        REQUIRE(result.member() == 42);
    }
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>, "future_non_copyable_single_task") {
    sut = async(make_executor<0>(), [] { return move_only(42); });

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE(result.member() == 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_then_non_copyable_detach") {
    atomic_bool check{false};
    {
        async(make_executor<0>(), [&_check = check] {
            _check = true;
            return move_only(42);
        }).detach();
    }
    while (!check) {
        invoke_waiting([] { this_thread::sleep_for(chrono::milliseconds(1)); });
    }
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>, "future_non_copyable_capture") {
    move_only m{42};

    sut = async(make_executor<0>(), [&_m = m] { return move_only(_m.member()); });

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE(result.member() == 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(
    test_fixture<stlab::move_only>,
    "future_copyable_with_non_copyable_as_continuation_with_same_scheduler_then_on_rvalue") {
    {
        sut =
            async(make_executor<0>(), [] { return 42; }).then([](auto x) { return move_only(x); });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
    {
        sut = async(make_executor<0>(), [] { return 42; }) | [](auto x) { return move_only(x); };

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<stlab::move_only>,
    "future_copyable_with_non_copyable_as_continuation_with_different_scheduler_then_on_rvalue") {
    {
        sut = async(make_executor<0>(), [] { return 42; }).then(make_executor<1>(), [](auto x) {
            return move_only(x);
        });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        sut = async(make_executor<0>(), [] { return 42; }) |
              (executor{make_executor<1>()} & [](auto x) { return move_only(x); });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<stlab::move_only>,
    "future_copyable_with_non_copyable_as_continuation_with_same_scheduler_then_on_lvalue") {
    {
        auto interim = async(make_executor<0>(), [] { return 42; });

        sut = interim.then([](auto x) { return move_only(x); });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
    {
        auto interim = async(make_executor<0>(), [] { return 42; });

        sut = interim | [](auto x) { return move_only(x); };

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<stlab::move_only>,
    "future_copyable_with_non_copyable_as_continuation_with_different_scheduler_then_on_lvalue") {
    {
        auto interim = async(make_executor<0>(), [] { return 42; });

        sut = interim.then(make_executor<1>(), [](auto x) { return move_only(x); });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }
    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();
    {
        auto interim = async(make_executor<0>(), [] { return 42; });

        sut = interim | (executor{make_executor<1>()} & [](auto x) { return move_only(x); });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_non_copyable_as_continuation_with_same_scheduler_then_on_rvalue") {
    {
        sut = async(make_executor<0>(), [] { return move_only(42); }).then([](auto&& x) {
            return move_only(x.member() * 2);
        });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42 * 2);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
    {
        sut = async(make_executor<0>(), [] { return move_only(42); }) |
              [](auto&& x) { return move_only(x.member() * 2); };

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42 * 2);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_non_copyable_as_continuation_with_different_scheduler_then_on_rvalue") {
    {
        sut = async(make_executor<0>(), [] {
                  return move_only(42);
              }).then(make_executor<1>(), [](auto x) { return move_only(x.member() * 2); });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42 * 2);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        sut = async(make_executor<0>(), [] { return move_only(42); }) |
              (executor{make_executor<1>()} & [](auto x) { return move_only(x.member() * 2); });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42 * 2);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
        REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_async_move_only_move_captured_to_result") {
    {
        sut =
            async(make_executor<0>(), [] { return move_only{42}; }).then([](auto x) { return x; });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
    {
        sut = async(make_executor<0>(), [] { return move_only{42}; }) | [](auto x) { return x; };

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_async_moving_move_only_capture_to_result") {
    move_only m{42};

    sut = async(make_executor<0>(), [&_m = m] { return std::move(_m); });

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE(result.member() == 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_async_mutable_move_move_only_capture_to_result") {
    move_only m{42};

    sut = async(make_executor<0>(), [&_m = m]() { return std::move(_m); });

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE(result.member() == 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_continuation_moving_move_only_capture_to_result") {
    move_only m{42};

    sut = async(make_executor<0>(), [] { return move_only{10}; }).then([&_m = m](auto) {
        return std::move(_m);
    });

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE(result.member() == 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_continuation_async_mutable_move_move_only_capture_to_result") {
    {
        move_only m{42};

        sut = async(make_executor<0>(), []() { return move_only{10}; }).then([&_m = m](auto) {
            return std::move(_m);
        });

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
    {
        move_only m{42};

        sut = async(make_executor<0>(), []() { return move_only{10}; }) |
              [&_m = m](auto) { return std::move(_m); };

        check_valid_future(sut);
        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>, "reduction_future_move_only_to_move_only") {
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] {
                  _flag = true;
                  return move_only(42);
              }).then([&_flag = second](auto&& x) {
            return async(
                default_executor,
                [&_flag](auto&& x) {
                    _flag = true;
                    return std::forward<move_only>(x);
                },
                std::forward<move_only>(x));
        });

        auto result = await(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(result.member() == 42);
    }
    {
        bool first{false};
        bool second{false};

        sut = async(immediate_executor, [&_flag = first] {
                  _flag = true;
                  return move_only(42);
              }).then([&_flag = second](auto&& x) {
            return async(
                immediate_executor,
                [&_flag](auto&& x) {
                    _flag = true;
                    return std::forward<move_only>(x);
                },
                std::forward<move_only>(x));
        });

        auto result = await(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(result.member() == 42);
    }
}

namespace stlab {

// specializing std::vector, so that the framework can detect correctly
// if std::vector<move_only> is copyable or only moveable
template <template <typename> class test, typename T, typename A>
struct smart_test<test, std::vector<T, A>> : test<T> {};

} // namespace stlab

TEST_CASE_FIXTURE(test_fixture<std::vector<stlab::move_only>>,
                  "future_continuation_async_move_only_container") {
    {
        sut = async(make_executor<0>(), []() {
                  std::vector<move_only> result;
                  result.emplace_back(10);
                  result.emplace_back(42);

                  return result;
              }).then([](auto x) {
            x.emplace_back(50);
            return x;
        });

        check_valid_future(sut);
        auto result = stlab::await(std::move(sut));

        REQUIRE(result.size() == 3u);
        REQUIRE(result[0].member() == 10);
        REQUIRE(result[1].member() == 42);
        REQUIRE(result[2].member() == 50);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
    {
        sut = async(make_executor<0>(),
                    []() {
                        std::vector<move_only> result;
                        result.emplace_back(10);
                        result.emplace_back(42);

                        return result;
                    }) |
              [](auto x) {
                  x.emplace_back(50);
                  return x;
              };

        check_valid_future(sut);
        auto result = stlab::await(std::move(sut));

        REQUIRE(result.size() == 3u);
        REQUIRE(result[0].member() == 10);
        REQUIRE(result[1].member() == 42);
        REQUIRE(result[2].member() == 50);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_single_task") {
    sut = async(make_executor<0>(), [] { return 42; });

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((*sut.get_try() == 42));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_single_task_get_try_on_rvalue") {
    sut = async(make_executor<0>(), [] { return 42; });

    auto test_result_1 = std::move(sut).get_try(); // test for r-value implementation
    (void)test_result_1;
    wait_until_future_completed(copy(sut));
    auto test_result_2 = std::move(sut).get_try();

    REQUIRE((*sut.get_try() == 42));
    REQUIRE((*test_result_2 == 42));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_int_single_task_detached") {
    atomic_bool check{false};
    {
        auto detached = async(make_executor<0>(), [&_check = check] {
            _check = true;
            return 42;
        });
        detached.detach();
    }
    while (!check) {
        invoke_waiting([] { this_thread::sleep_for(chrono::milliseconds(1)); });
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_two_tasks_with_same_scheduler_then_on_rvalue") {
    {
        sut = async(make_executor<0>(), [] { return 42; }).then([](auto x) { return x + 42; });

        check_valid_future(sut);
        wait_until_future_completed(copy(sut));

        REQUIRE((*sut.get_try() == 42 + 42));
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
    {
        sut = async(make_executor<0>(), [] { return 42; }) | [](auto x) { return x + 42; };

        check_valid_future(sut);
        wait_until_future_completed(copy(sut));

        REQUIRE((*sut.get_try() == 42 + 42));
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_two_tasks_with_same_scheduler_then_on_lvalue") {
    {
        auto interim = async(make_executor<0>(), [] { return 42; });

        sut = interim.then([](auto x) { return x + 42; });

        check_valid_future(sut);
        wait_until_future_completed(copy(sut));

        REQUIRE((*sut.get_try() == 42 + 42));
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
    {
        auto interim = async(make_executor<0>(), [] { return 42; });

        sut = interim | [](auto x) { return x + 42; };

        check_valid_future(sut);
        wait_until_future_completed(copy(sut));

        REQUIRE((*sut.get_try() == 42 + 42));
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_two_tasks_with_different_scheduler") {
    sut = async(make_executor<0>(), [] { return 42; }).then(make_executor<1>(), [](auto x) {
        return x + 42;
    });

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((*sut.get_try() == 42 + 42));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_void_int_two_tasks_with_same_scheduler") {
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }).then([&_p = p] {
            _p += 42;
            return _p.load();
        });

        check_valid_future(sut);
        wait_until_future_completed(std::move(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }) | [&_p = p] {
            _p += 42;
            return _p.load();
        };

        check_valid_future(sut);
        wait_until_future_completed(copy(sut));

        REQUIRE(p == 42 + 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_void_int_two_tasks_with_different_scheduler") {
    atomic_int p{0};

    sut = async(make_executor<0>(), [&_p = p] { _p = 42; }).then(make_executor<1>(), [&_p = p] {
        _p += 42;
        return _p.load();
    });

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE(p == 42 + 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

/*
    sut - f - f
*/
TEST_CASE_FIXTURE(test_fixture<int>, "future_int_three_tasks_with_same_scheduler") {
    {
        sut = async(make_executor<0>(), [] {
                  return 42;
              }).then([](auto x) {
                    return x + 42;
                }).then([](auto x) { return x + 42; });

        check_valid_future(sut);
        wait_until_future_completed(copy(sut));

        REQUIRE((*sut.get_try() == 42 + 42 + 42));
        REQUIRE(custom_scheduler<0>::usage_counter() >= 3);
    }
    {
        sut = async(make_executor<0>(), [] { return 42; }) | [](auto x) { return x + 42; } |
              [](auto x) { return x + 42; };

        check_valid_future(sut);
        wait_until_future_completed(copy(sut));

        REQUIRE((*sut.get_try() == 42 + 42 + 42));
        REQUIRE(custom_scheduler<0>::usage_counter() >= 3);
    }
}

/*
        f1
       /
    sut
       \
        f2
*/
TEST_CASE_FIXTURE(test_fixture<int>, "future_int_Y_formation_tasks_with_same_scheduler") {
    {
        sut = async(make_executor<0>(), [] { return 42; });
        auto f1 = sut.then([](auto x) { return x + 42; });
        auto f2 = sut.then([](auto x) { return x + 4177; });

        check_valid_future(sut, f1, f2);
        wait_until_future_completed(copy(f1), copy(f2));

        REQUIRE(*f1.get_try() == 42 + 42);
        REQUIRE(*f2.get_try() == 42 + 4177);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 3);
    }
    {
        sut = async(make_executor<0>(), [] { return 42; });
        auto f1 = sut | [](auto x) { return x + 42; };
        auto f2 = sut | [](auto x) { return x + 4177; };

        check_valid_future(sut, f1, f2);
        wait_until_future_completed(copy(f1), copy(f2));

        REQUIRE(*f1.get_try() == 42 + 42);
        REQUIRE(*f2.get_try() == 42 + 4177);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 3);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "reduction_future_void_to_int") {
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] { _flag = true; }).then([&_flag = second] {
            return async(default_executor, [&_flag] {
                _flag = true;
                return 42;
            });
        });

        wait_until_future_completed(copy(sut));

        REQUIRE(first);
        REQUIRE(second);
        REQUIRE((*sut.get_try() == 42));
    }
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] { _flag = true; }) | [&_flag = second] {
            return async(default_executor, [&_flag] {
                _flag = true;
                return 42;
            });
        };

        wait_until_future_completed(copy(sut));

        REQUIRE(first);
        REQUIRE(second);
        REQUIRE((*sut.get_try() == 42));
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "reduction_future_int_to_int") {
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] {
                  _flag = true;
                  return 42;
              }).then([&_flag = second](auto x) {
            return async(
                default_executor,
                [&_flag](auto x) {
                    _flag = true;
                    return x + 42;
                },
                x);
        });

        wait_until_future_completed(copy(sut));

        REQUIRE(first);
        REQUIRE(second);
        REQUIRE((*sut.get_try() == 84));
    }
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor,
                    [&_flag = first] {
                        _flag = true;
                        return 42;
                    }) |
              [&_flag = second](auto x) {
                  return async(
                      default_executor,
                      [&_flag](auto x) {
                          _flag = true;
                          return x + 42;
                      },
                      x);
              };

        wait_until_future_completed(copy(sut));

        REQUIRE(first);
        REQUIRE(second);
        REQUIRE((*sut.get_try() == 84));
    }
}

// ----------------------------------------------------------------------------
//                             Error cases
// ----------------------------------------------------------------------------

TEST_CASE_FIXTURE(test_fixture<void>, "future_void_single_task_error") {
    sut = async(make_executor<0>(), [] { throw test_exception("failure"); });

    wait_until_future_fails<test_exception>(copy(sut));
    check_failure<test_exception>(sut, "failure");
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_void_two_tasks_error_in_1st_task_with_same_scheduler") {
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [] { throw test_exception("failure"); }).then([&_p = p] {
            _p = 42;
        });

        wait_until_future_fails<test_exception>(copy(sut));
        check_failure<test_exception>(sut, "failure");
        REQUIRE(p == 0);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [] { throw test_exception("failure"); }) |
              [&_p = p] { _p = 42; };

        wait_until_future_fails<test_exception>(copy(sut));
        check_failure<test_exception>(sut, "failure");
        REQUIRE(p == 0);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_void_two_tasks_error_in_2nd_task_with_same_scheduler") {
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }).then([&_p = p] {
            (void)_p;
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(sut));

        check_failure<test_exception>(sut, "failure");
        REQUIRE(p == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
    {
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }) | [&_p = p] {
            (void)_p;
            throw test_exception("failure");
        };

        wait_until_future_fails<test_exception>(copy(sut));

        check_failure<test_exception>(sut, "failure");
        REQUIRE(p == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "reduction_future_void_to_void_error") {
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] { _flag = true; }).then([&_flag = second] {
            return async(default_executor, [&_flag] {
                _flag = true;
                throw test_exception("failure");
            });
        });

        wait_until_future_fails<test_exception>(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] { _flag = true; }) | [&_flag = second] {
            return async(default_executor, [&_flag] {
                _flag = true;
                throw test_exception("failure");
            });
        };

        wait_until_future_fails<test_exception>(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "reduction_future_move_only_to_void_when_inner_future_fails") {
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] {
                  _flag = true;
                  return move_only(42);
              }).then([&_check = second](auto&& x) {
            return async(
                default_executor,
                [&_check](auto&&) {
                    _check = true;
                    throw test_exception("failure");
                },
                std::forward<move_only>(x));
        });

        wait_until_future_fails<test_exception>(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
    {
        bool first{false};
        bool second{false};

        sut = async(immediate_executor, [&_flag = first] {
                  _flag = true;
                  return move_only(42);
              }).then([&_check = second](auto&& x) {
            return async(
                immediate_executor,
                [&_check](auto&&) {
                    _check = true;
                    throw test_exception("failure");
                },
                std::forward<move_only>(x));
        });

        wait_until_future_fails<test_exception>(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_single_task_error") {
    sut = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_two_tasks_error_in_1st_task_with_same_scheduler") {
    {
        custom_scheduler<0>::reset();
        int p = 0;

        sut = async(make_executor<0>(), [] {
                  throw test_exception("failure");
              }).then([&_p = p]() -> int {
            _p = 42;
            return _p;
        });

        wait_until_future_fails<test_exception>(copy(sut));

        check_failure<test_exception>(sut, "failure");
        REQUIRE(p == 0);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
    {
        custom_scheduler<0>::reset();
        int p = 0;

        sut = async(make_executor<0>(), [] { throw test_exception("failure"); }) |
              [&_p = p]() -> int {
            _p = 42;
            return _p;
        };

        wait_until_future_fails<test_exception>(copy(sut));

        check_failure<test_exception>(sut, "failure");
        REQUIRE(p == 0);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_two_tasks_error_in_2nd_task_with_same_scheduler") {
    {
        custom_scheduler<0>::reset();
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }).then([]() -> int {
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(sut));

        check_failure<test_exception>(sut, "failure");
        REQUIRE(p == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
    {
        custom_scheduler<0>::reset();
        atomic_int p{0};

        sut = async(make_executor<0>(), [&_p = p] { _p = 42; }) |
              []() -> int { throw test_exception("failure"); };

        wait_until_future_fails<test_exception>(copy(sut));

        check_failure<test_exception>(sut, "failure");
        REQUIRE(p == 42);
        REQUIRE(custom_scheduler<0>::usage_counter() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_int_Y_formation_tasks_with_failing_1st_task") {
    atomic_int p{0};

    sut = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    auto f1 = sut.then(make_executor<0>(), [&_p = p](auto x) -> int {
        _p += 1;
        return x + 42;
    });
    auto f2 = sut.then(make_executor<0>(), [&_p = p](auto x) -> int {
        _p += 1;
        return x + 4177;
    });

    wait_until_future_fails<test_exception>(copy(f1), copy(f2));

    check_failure<test_exception>(f1, "failure");
    check_failure<test_exception>(f2, "failure");
    REQUIRE(p == 0);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>,
                  "future_int_Y_formation_tasks_where_one_of_the_2nd_task_failing") {
    sut = async(make_executor<0>(), []() -> int { return 42; });
    auto f1 = sut.then(make_executor<0>(), [](auto) -> int { throw test_exception("failure"); });
    auto f2 = sut.then(make_executor<0>(), [](auto x) -> int { return x + 4711; });

    wait_until_future_completed(copy(f2));
    wait_until_future_fails<test_exception>(copy(f1));

    check_failure<test_exception>(f1, "failure");
    REQUIRE(*f2.get_try() == 42 + 4711);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 3);
}

TEST_CASE_FIXTURE(test_fixture<int>,
                  "future_int_Y_formation_tasks_where_both_of_the_2nd_task_failing") {
    sut = async(make_executor<0>(), []() -> int { return 42; });
    auto f1 = sut.then(make_executor<0>(), [](auto) -> int { throw test_exception("failure"); });
    auto f2 = sut.then(make_executor<0>(), [](auto) -> int { throw test_exception("failure"); });

    wait_until_future_fails<test_exception>(copy(f1), copy(f2));

    check_failure<test_exception>(f1, "failure");
    check_failure<test_exception>(f2, "failure");
    REQUIRE(custom_scheduler<0>::usage_counter() >= 3);
}

TEST_CASE_FIXTURE(test_fixture<int>, "reduction_future_void_to_int_error") {
    atomic_bool first{false};
    atomic_bool second{false};

    sut = async(default_executor, [&_flag = first] {
              _flag = true;
          }).then([&_flag = second]() -> future<int> {
        (void)_flag;
        throw test_exception("failure");
    });

    wait_until_future_fails<test_exception>(std::move(sut));

    REQUIRE(first);
    REQUIRE(!second);
}

TEST_CASE_FIXTURE(test_fixture<int>, "reduction_future_int_to_int_error") {
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] {
                  _flag = true;
                  return 42;
              }).then([&_flag = second](auto x) {
            return async(
                default_executor,
                [&_flag](auto) -> int {
                    _flag = true;
                    throw test_exception("failure");
                },
                x);
        });

        wait_until_future_fails<test_exception>(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor,
                    [&_flag = first] {
                        _flag = true;
                        return 42;
                    }) |
              [&_flag = second](auto x) {
                  return async(
                      default_executor,
                      [&_flag](auto) -> int {
                          _flag = true;
                          throw test_exception("failure");
                      },
                      x);
              };

        wait_until_future_fails<test_exception>(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "reduction_future_move_only_to_move_only_when_inner_future_fails") {
    {
        atomic_bool first{false};
        atomic_bool second{false};

        sut = async(default_executor, [&_flag = first] {
                  _flag = true;
                  return move_only(42);
              }).then([&_flag = second](auto&& x) {
            return async(
                default_executor,
                [&_flag](auto&&) -> move_only {
                    _flag = true;
                    throw test_exception("failure");
                },
                std::forward<move_only>(x));
        });

        wait_until_future_fails<test_exception>(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
    {
        bool first{false};
        bool second{false};

        sut = async(immediate_executor, [&_flag = first] {
                  _flag = true;
                  return move_only(42);
              }).then([&_flag = second](auto&& x) {
            return async(
                immediate_executor,
                [&_flag](auto&&) -> move_only {
                    _flag = true;
                    throw test_exception("failure");
                },
                std::forward<move_only>(x));
        });

        wait_until_future_fails<test_exception>(std::move(sut));

        REQUIRE(first);
        REQUIRE(second);
    }
}
