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
    result = co_await std::move(x);
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

auto resume_on_executor_coroutine(future<int> f,
                                  std::atomic_int& result,
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

namespace {

future<int> resume_on_default_executor() {
    auto result = co_await async(default_executor, [] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return 42;
    });
    co_return result;
}

future<int> resume_on_with_explicit_resume_on() {
    auto result = co_await resume_on(default_executor, async(default_executor, [] {
                                         std::this_thread::sleep_for(std::chrono::seconds(1));
                                         return 42;
                                     }));
    co_return result;
}

} // namespace

TEST_CASE("resume_on_with_cancelled_future") {
    {
        auto result(resume_on_default_executor());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

TEST_CASE("resume_on_explicit_with_cancelled_future") {
    {
        auto result(resume_on_with_explicit_resume_on());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
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

namespace {

std::coroutine_handle<> g_escaped_handle;

struct escape_awaitable {
    bool await_ready() const { return false; }
    void await_suspend(std::coroutine_handle<> ch) { g_escaped_handle = ch; }
    int await_resume() { return 0; }
};

future<int> escape_handle_then_resume() {
    int x = co_await escape_awaitable{};
    co_return x + 42;
}

} // namespace

TEST_CASE("generic_await_escape_then_resume_after_future_reset") {
    {
        auto f = escape_handle_then_resume();
        // Coroutine suspended at escape_awaitable; handle stored in global.
        REQUIRE(g_escaped_handle);
    }
    // Future destroyed; we gave up ownership so guard did not destroy the handle.
    REQUIRE(g_escaped_handle);
    g_escaped_handle.resume();
    // Coroutine completes; final_awaiter destroys the handle.
}

TEST_CASE("throwing_operation_in_coawait_future") {
    auto f = []() -> future<int> {
        try {
            co_await make_exceptional_future<void>(
                std::make_exception_ptr(test_exception("failure")), default_executor);
        } catch (const test_exception&) {
            co_return 1;
        }
        co_return 0;
    }();
    REQUIRE(std::move(f).get_ready() == 1);
}
