/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/ready_future.hpp>
#include <stlab/config.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

int completed = 0;
bool submitting = true;

/// Fails the process when a cooperative scheduling contract is violated.
void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::abort();
    }
}

/// Completes one queued task and verifies it did not run inline.
void complete() noexcept {
    require(!submitting, "executor ran inline");
    ++completed;
}

#if STLAB_STD_COROUTINES()
/// Suspends without blocking until the supplied cooperative future completes.
auto coroutine_result(stlab::future<int> source) -> stlab::future<int> {
    co_return co_await std::move(source);
}
#endif

} // namespace

int main(int argc, char** argv) {
    using namespace std::chrono_literals;
    require(!stlab::stlab_v2_default_executor_supports_blocking(), "core permits blocking");

#if STLAB_STD_COROUTINES()
    if (argc > 1 && std::string_view{argv[1]} == "coroutine") {
        auto [set, result] = stlab::package<int()>(stlab::immediate_executor, [] { return 42; });
        coroutine_result(std::move(result))
            .then(stlab::immediate_executor,
                  [](int value) noexcept {
                      require(value == 42, "coroutine lost result");
                      main_executor_test::finish(true, "");
                  })
            .detach();
        main_executor_test::run(argc, argv, [&] {
            stlab::default_executor([set = std::move(set)]() mutable noexcept { set(); });
        });
    }
#endif

    if (argc > 1) {
        std::set_terminate([] {
            std::fputs("EXPECTED_STLAB_TERMINATE\n", stderr);
            std::abort();
        });
        if (std::string_view{argv[1]} == "invoke_waiting") {
            stlab::invoke_waiting([] { std::_Exit(1); });
        } else {
            auto [set, result] =
                stlab::package<int()>(stlab::immediate_executor, [] { return 42; });
            stlab::await(std::move(result));
        }
        return 1;
    }

    require(stlab::await(stlab::make_ready_future(42, stlab::immediate_executor)) == 42,
            "ready await value");
    stlab::await(stlab::make_ready_future(stlab::immediate_executor));
    auto failed = stlab::make_exceptional_future<int>(
        std::make_exception_ptr(std::runtime_error{"expected"}), stlab::immediate_executor);
    bool exception_seen = false;
    try {
        stlab::await(std::move(failed));
    } catch (const std::runtime_error&) {
        exception_seen = true;
    }
    require(exception_seen, "ready await exception");
    auto polled_failure = stlab::await_for(
        stlab::make_exceptional_future<void>(
            std::make_exception_ptr(std::runtime_error{"expected"}), stlab::immediate_executor),
        24h);
    exception_seen = false;
    try {
        (void)polled_failure.get_try();
    } catch (const std::runtime_error&) {
        exception_seen = true;
    }
    require(exception_seen, "poll lost ready exception");
    auto [set_void, void_result] = stlab::package<void()>(stlab::immediate_executor, [] {});
    void_result = stlab::await_for(std::move(void_result), 24h);
    require(void_result.valid() && !void_result.is_ready(), "poll lost pending void future");
    set_void();
    void_result = stlab::await_for(std::move(void_result), 24h);
    require(void_result.is_ready(), "void polling did not observe completion");
    stlab::await(std::move(void_result));

    auto [set, result] = stlab::package<int()>(stlab::immediate_executor, [] { return 42; });
    result = stlab::await_for(std::move(result), 24h);
    require(result.valid() && !result.is_ready(), "poll lost pending future");
    require(!result.get_try(), "poll produced a premature value");
    result = stlab::await_for(std::move(result), -1ns);
    require(result.valid() && !result.is_ready(), "negative-timeout poll lost future");
    result = stlab::await_for(std::move(result), 0ns);
    require(result.valid() && !result.is_ready(), "zero-timeout poll lost future");

    main_executor_test::run(argc, argv, [&] {
        stlab::high_executor(&complete);
        stlab::default_executor(&complete);
        stlab::low_executor(&complete);
        stlab::default_executor([set = std::move(set)]() mutable noexcept { set(); });
        stlab::main_executor([result = std::move(result)]() mutable noexcept {
            require(completed == 3, "executor tasks did not all complete");
            result = stlab::await_for(std::move(result), 24h);
            require(result.is_ready(), "pending result did not complete");
            require(stlab::await(std::move(result)) == 42, "later await value");
            main_executor_test::finish(true, "");
        });
        submitting = false;
    });
}
