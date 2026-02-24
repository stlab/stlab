/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <cstddef>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/ready_future.hpp>
#include <stlab/test/model.hpp>
#include <stlab/utility.hpp>

#include "future_test_helper.hpp"

using namespace stlab;
using namespace future_test_helper;

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args_int_with_one_element") {
    auto f1 = async(make_executor<0>(), [] { return 42; });
    sut = when_all(make_executor<1>(), [](auto x) { return x + x; }, f1);

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE(*sut.get_try() == 42 + 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args_int_with_many_elements") {
    auto f1 = async(make_executor<0>(), [] { return 1; });
    auto f2 = async(make_executor<0>(), [] { return 2; });
    auto f3 = async(make_executor<0>(), [] { return 3; });
    auto f4 = async(make_executor<0>(), [] { return 5; });

    sut = when_all(
        make_executor<1>(),
        [](int x1, int x2, int x3, int x4) { return 7 * x1 + 11 * x2 + 13 * x3 + 17 * x4; }, f1, f2,
        f3, f4);

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE(*sut.get_try() == 1 * 7 + 2 * 11 + 3 * 13 + 5 * 17);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args_int_with_ready_element") {
    sut = when_all(
        make_executor<1>(), [](auto x) { return x + x; },
        make_ready_future<int>(42, immediate_executor));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE(*sut.get_try() == 42 + 42);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args_int_with_executor") {
    sut = stlab::when_all(
        make_executor<1>(), [](auto x, auto y) { return x + y; },
        stlab::make_ready_future<int>(42, stlab::immediate_executor),
        stlab::make_ready_future<int>(42, stlab::immediate_executor));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE(*sut.get_try() == 42 + 42);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args_int_with_two_ready_element") {
    sut = when_all(
        make_executor<1>(), [](auto x, auto y) { return x + y; },
        make_ready_future<int>(42, immediate_executor),
        make_ready_future<int>(42, immediate_executor));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE(*sut.get_try() == 42 + 42);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args") {
    auto main_thread_id = std::this_thread::get_id();
    auto r = when_all(
        make_executor<1>(), [] { return std::this_thread::get_id(); },
        make_ready_future(stlab::immediate_executor));

    wait_until_future_completed(copy(r));

    REQUIRE(main_thread_id != *r.get_try());
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_Arguments_with_mutable_task") {
    struct mutable_int {
        int i = 0;
        auto operator()() {
            ++i;
            return i;
        }
    };
    mutable_int func1;
    mutable_int func2;

    auto r = when_all(
        stlab::default_executor, [](auto f1, auto f2) { return f1() + f2(); },
        async(stlab::default_executor,
              [func = func1]() mutable {
                  func();
                  return func;
              }),
        async(stlab::default_executor, [func = func2]() mutable {
            func();
            return func;
        }));

    REQUIRE(stlab::await(std::move(r)) == 4);
}
TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_arguments_with_mutable_move_onlytask") {
    struct mutable_move_only {
        move_only i;
        auto operator()() {
            i = move_only{i.member() + 1};
            return std::move(i);
        }
    };
    mutable_move_only func1;
    mutable_move_only func2;

    auto r = when_all(
        stlab::default_executor, [](auto f1, auto f2) { return f1().member() + f2().member(); },
        async(stlab::default_executor,
              [func = std::move(func1)]() mutable {
                  func();
                  return std::move(func);
              }),
        async(stlab::default_executor, [func = std::move(func2)]() mutable {
            func();
            return std::move(func);
        }));

    REQUIRE(stlab::await(std::move(r)) == 4);
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_when_all_args_move_only_with_one_element") {
    auto f1 = async(make_executor<0>(), [] { return move_only(42); });
    sut = when_all(
        make_executor<1>(), [](auto x) { return move_only(x.member() + x.member()); },
        std::move(f1));

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE(result.member() == 42 + 42);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>,
                  "future_when_all_args_move_only_with_many_elements") {
    auto f1 = async(make_executor<0>(), [] { return move_only(1); });
    auto f2 = async(make_executor<0>(), [] { return move_only(2); });
    auto f3 = async(make_executor<0>(), [] { return move_only(3); });
    auto f4 = async(make_executor<0>(), [] { return move_only(5); });

    sut = when_all(
        make_executor<1>(),
        [](auto x1, auto x2, auto x3, auto x4) {
            return move_only(7 * x1.member() + 11 * x2.member() + 13 * x3.member() +
                             17 * x4.member());
        },
        std::move(f1), std::move(f2), std::move(f3), std::move(f4));

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE(result.member() == 1 * 7 + 2 * 11 + 3 * 13 + 5 * 17);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<std::string>, "future_when_all_args_with_different_types") {
    auto f1 = async(make_executor<0>(), [] { return 1; });
    auto f2 = async(make_executor<0>(), [] { return 3.1415; });
    auto f3 = async(make_executor<0>(), [] { return std::string("Don't panic!"); });
    auto f4 = async(make_executor<0>(), [] { return std::vector<size_t>(2, 3); });

    sut = when_all(
        make_executor<1>(),
        [](int x1, double x2, const std::string& x3, const std::vector<size_t>& x4) {
            std::stringstream st;
            st << x1 << " " << x2 << " " << x3 << " " << x4[0] << " " << x4[1];
            return st.str();
        },
        f1, f2, f3, f4);

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((*sut.get_try() == std::string("1 3.1415 Don't panic! 3 3")));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

// ----------------------------------------------------------------------------
//                             Error cases
// ----------------------------------------------------------------------------

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args_int_failure_with_one_element") {
    auto f1 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    sut = when_all(make_executor<1>(), [](auto x) { return x + x; }, f1);

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args_int_with_many_elements_one_failing") {
    auto f1 = async(make_executor<0>(), [] { return 1; });
    auto f2 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    auto f3 = async(make_executor<0>(), [] { return 3; });
    auto f4 = async(make_executor<0>(), [] { return 5; });

    sut = when_all(
        make_executor<1>(),
        [](int x1, int x2, int x3, int x4) { return 7 * x1 + 11 * x2 + 13 * x3 + 17 * x4; }, f1, f2,
        f3, f4);

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_args_int_with_many_elements_all_failing") {
    auto f1 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    auto f2 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    auto f3 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    auto f4 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });

    sut = when_all(
        make_executor<1>(),
        [](int x1, int x2, int x3, int x4) { return 7 * x1 + 11 * x2 + 13 * x3 + 17 * x4; }, f1, f2,
        f3, f4);

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<std::string>,
                  "future_when_all_args_with_different_types_one_failing") {
    auto f1 = async(make_executor<0>(), [] { return 1; });
    auto f2 = async(make_executor<0>(), [] { return 3.1415; });
    auto f3 = async(make_executor<0>(), []() -> std::string { throw test_exception("failure"); });
    auto f4 = async(make_executor<0>(), [] { return std::vector<size_t>(2, 3); });

    sut = when_all(
        make_executor<1>(),
        [](int x1, double x2, const std::string& x3, const std::vector<size_t>& x4) {
            std::stringstream st;
            st << x1 << " " << x2 << " " << x3 << " " << x4[0] << " " << x4[1];
            return st.str();
        },
        f1, f2, f3, f4);

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<std::string>,
                  "future_when_all_args_with_different_types_all_failing") {
    auto f1 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    auto f2 = async(make_executor<0>(), []() -> double { throw test_exception("failure"); });
    auto f3 = async(make_executor<0>(), []() -> std::string { throw test_exception("failure"); });
    auto f4 =
        async(make_executor<0>(), []() -> std::vector<size_t> { throw test_exception("failure"); });

    sut = when_all(
        make_executor<1>(),
        [](int x1, double x2, const std::string& x3, const std::vector<size_t>& x4) {
            std::stringstream st;
            st << x1 << " " << x2 << " " << x3 << " " << x4[0] << " " << x4[1];
            return st.str();
        },
        f1, f2, f3, f4);

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}
