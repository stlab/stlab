/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <array>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/ready_future.hpp>
#include <stlab/test/model.hpp>

#include "future_test_helper.hpp"
#include "stlab/concurrency/immediate_executor.hpp"

using namespace stlab;
using namespace future_test_helper;

using lock_t = std::unique_lock<std::mutex>;

template <class T>
auto range_pair(T& r) {
    return std::pair{r.begin(), r.end()};
}

TEST_CASE("future_when_any_void_void_empty_range") {
    auto result = when_any(immediate_executor, when_any_result,
                           std::pair<future<void>*, future<void>*>{nullptr, nullptr});
    REQUIRE_THROWS_AS((void)result.get_try(), future_error);
}

TEST_CASE("future_when_any_int_void_empty_range") {
    auto result = when_any(immediate_executor, when_any_result,
                           std::pair<future<int>*, future<int>*>{nullptr, nullptr});
    REQUIRE_THROWS_AS((void)result.get_try(), future_error);
}

TEST_CASE("future_when_any_void_void_range_with_one_element") {
    auto [p0, f0] = package<void()>(immediate_executor, [] {});
    auto result = when_any(immediate_executor, when_any_result, std::pair{&f0, &f0 + 1});

    REQUIRE(!result.is_ready());
    default_executor(std::move(p0));
    REQUIRE((std::size_t{0} == await(std::move(result))));
}

TEST_CASE("future_when_any_int_void_range_with_one_element") {
    auto [p0, f0] = package<int()>(immediate_executor, [] { return 0; });
    auto result = when_any(immediate_executor, when_any_result, std::pair{&f0, &f0 + 1});

    REQUIRE(!result.is_ready());
    default_executor(std::move(p0));
    REQUIRE((std::pair{0, std::size_t{0}} == await(std::move(result))));
}

TEST_CASE("future_when_any_int_void_range_with_many_elements_first_succeeds") {
    auto [p0, f0] = package<int()>(immediate_executor, [] { return 0; });
    auto [p1, f1] = package<int()>(immediate_executor, [] { return 1; });
    auto [p2, f2] = package<int()>(immediate_executor, [] { return 2; });
    auto [p3, f3] = package<int()>(immediate_executor, [] { return 3; });

    std::array a{std::move(f0), std::move(f1), std::move(f2), std::move(f3)};

    auto result = when_any(immediate_executor, when_any_result, range_pair(a));

    REQUIRE(!result.is_ready());
    default_executor(std::move(p0));
    REQUIRE((std::pair{0, std::size_t{0}} == await(std::move(result))));
}

TEST_CASE("future_when_any_int_void_range_with_many_elements_middle_succeeds") {
    auto [p0, f0] = package<int()>(immediate_executor, [] { return 0; });
    auto [p1, f1] = package<int()>(immediate_executor, [] { return 1; });
    auto [p2, f2] = package<int()>(immediate_executor, [] { return 2; });
    auto [p3, f3] = package<int()>(immediate_executor, [] { return 3; });

    std::array a{std::move(f0), std::move(f1), std::move(f2), std::move(f3)};

    auto result = when_any(immediate_executor, when_any_result, range_pair(a));

    REQUIRE(!result.is_ready());
    default_executor(std::move(p2));
    REQUIRE((std::pair{2, std::size_t{2}} == await(std::move(result))));
}

TEST_CASE("future_when_any_int_void_range_with_many_elements_one_succeeds_all_other_fails") {
    auto [p0, f0] = package<int()>(immediate_executor, [] { return 0; });
    auto [p1, f1] = package<int()>(immediate_executor, [] { return 1; });
    auto [p2, f2] = package<int()>(immediate_executor, [] { return 2; });
    auto [p3, f3] = package<int()>(immediate_executor, [] { return 3; });

    std::array a{std::move(f0), std::move(f1), std::move(f2), std::move(f3)};

    auto result = when_any(immediate_executor, when_any_result, range_pair(a));

    p0.set_exception(std::make_exception_ptr(test_exception("failure")));
    p1.set_exception(std::make_exception_ptr(test_exception("failure")));
    p3.set_exception(std::make_exception_ptr(test_exception("failure")));
    REQUIRE(!result.is_ready());
    default_executor(std::move(p2));
    REQUIRE((std::pair{2, std::size_t{2}} == await(std::move(result))));
}

TEST_CASE("future_when_any_void_all_are_ready_at_the_beginning") {
    std::array a{make_ready_future(immediate_executor), make_ready_future(immediate_executor)};

    auto result = when_any(immediate_executor, when_any_result, range_pair(a));

    REQUIRE((0 == *result.get_try()));
}

TEST_CASE("future_when_any_int_void_range_with_many_elements_all_fails") {
    std::array a{make_exceptional_future<void>(std::make_exception_ptr(test_exception("failure")),
                                               immediate_executor),
                 make_exceptional_future<void>(
                     std::make_exception_ptr(test_exception("failure-final")), immediate_executor)};

    auto result = when_any(immediate_executor, when_any_result, range_pair(a));

    REQUIRE(result.is_ready());
    REQUIRE_THROWS_AS((void)result.get_try(), test_exception);
}

/*
     /  F1  \
    / / F2 \ \
start           sut
    \ \ F3 / /
     \  F4  /
*/
TEST_CASE("future_when_any_void_range_with_diamond_formation_elements") {
    auto [initial_promise, start] = package<int()>(immediate_executor, [] { return 42; });
    auto f1 = start | [](int x) { return x + 1; };
    auto f2 = start | [](int x) { return x + 2; };
    auto f3 = start | [](int x) { return x + 3; };
    auto f4 = start | [](int x) { return x + 4; };

    std::array a{std::move(f1), std::move(f2), std::move(f3), std::move(f4)};
    auto result = when_any(immediate_executor, when_any_result, range_pair(a));

    REQUIRE(!result.is_ready());
    default_executor(std::move(initial_promise));
    REQUIRE((std::pair{43, std::size_t{0}} == await(std::move(result))));
}

TEST_CASE("future_when_any_range_move_only") {
    auto [p0, f0] = package<move_only()>(immediate_executor, [] { return move_only(0); });
    auto [p1, f1] = package<move_only()>(immediate_executor, [] { return move_only(1); });

    std::array a{std::move(f0), std::move(f1)};

    auto result = when_any(immediate_executor, when_any_result, range_pair(a));

    REQUIRE(!result.is_ready());
    default_executor(std::move(p0));
    REQUIRE((std::pair{move_only(0), std::size_t{0}} == await(std::move(result))));
}
