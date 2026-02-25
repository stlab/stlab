/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/ready_future.hpp>
#include <stlab/test/model.hpp>

#include "future_test_helper.hpp"

using namespace stlab;
using namespace future_test_helper;

auto get_the_answer() -> stlab::future<int> { co_return 42; }

TEST_CASE("future_coroutine_int") {
    auto w = get_the_answer();

    REQUIRE(42 == await(std::move(w)));
}

auto get_move_only_answer() -> stlab::future<move_only> { co_return move_only{42}; }

TEST_CASE("future_coroutine_move_only") {
    auto w = get_move_only_answer();
    auto r = await(std::move(w));

    REQUIRE(42 == r.member());
}

auto just_wait() -> stlab::future<void> { co_return; }

TEST_CASE("future_coroutine_void") {
    auto w = just_wait();

    REQUIRE_NOTHROW(await(std::move(w)));
}

auto get_the_answer_with_failure() -> stlab::future<int> {
    co_return co_await stlab::async(stlab::default_executor, []() -> int {
        invoke_waiting([] { std::this_thread::sleep_for(std::chrono::milliseconds(1000)); });
        throw test_exception("failure");
    });
}

TEST_CASE("future_coroutine_int_failure") {
    auto w = get_the_answer_with_failure();

    REQUIRE_THROWS_WITH_AS(await(std::move(w)), "failure", test_exception);
}

auto get_the_answer_move_only_with_failure() -> stlab::future<move_only> {
    co_return co_await stlab::async(stlab::default_executor, []() -> move_only {
        invoke_waiting([] { std::this_thread::sleep_for(std::chrono::milliseconds(1000)); });
        throw test_exception("failure");
    });
}

TEST_CASE("future_coroutine_move_only_failure") {
    auto w = get_the_answer_move_only_with_failure();

    REQUIRE_THROWS_WITH_AS(await(std::move(w)), "failure", test_exception);
}

auto do_it(future<int> x, std::atomic_int& result) -> future<void> {
    int v = co_await std::move(x);
    result = v;
    std::cout << v << '\n';
    co_return;
}

TEST_CASE("future_coroutine_combined_void_int") {
    std::atomic_int intCheck{0};
    std::atomic_bool boolCheck{false};

    auto done = do_it(async(default_executor, [] { return 42; }), intCheck);
    auto hold = done.then([&boolCheck] { boolCheck = true; });

    await(std::move(hold));

    REQUIRE(intCheck == 42);
    REQUIRE(boolCheck.load());
}

auto resume_on_executor_coroutine(future<int> f, std::atomic_int& result,
                                  std::atomic_int* executor_usage_count) -> future<void> {
    int v = co_await resume_on(make_executor<0>(), std::move(f));
    result = v;
    if (executor_usage_count) *executor_usage_count = custom_scheduler<0>::usage_counter();
    co_return;
}

TEST_CASE("resume_on_resumes_coroutine_on_given_executor") {
    custom_scheduler<0>::reset();
    std::atomic_int result{0};
    std::atomic_int executor_usage_count{0};

    auto fut = async(default_executor, [] { return 42; });
    auto done = resume_on_executor_coroutine(std::move(fut), result, &executor_usage_count);
    await(std::move(done));

    REQUIRE(result == 42);
    REQUIRE(executor_usage_count.load() >= 1);
}

TEST_CASE("resume_on_already_ready_future_resumes_on_executor") {
    custom_scheduler<0>::reset();
    std::atomic_int result{0};
    std::atomic_int executor_usage_count{0};

    auto fut = make_ready_future(43, default_executor);
    auto done = resume_on_executor_coroutine(std::move(fut), result, &executor_usage_count);
    await(std::move(done));

    REQUIRE(result == 43);
    REQUIRE(executor_usage_count.load() >= 1);
}
