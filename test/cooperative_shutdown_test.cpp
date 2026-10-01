/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/main_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/pre_exit.hpp>

#include <emscripten.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<int> trace;
bool inside_shutdown = false;
bool shutdown_returned = false;
bool capture_destroyed = false;

/// Aborts when an observable shutdown guarantee is violated.
void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::abort();
    }
}

/// Records a callback that must not run inline during shutdown.
void record(int value) {
    require(!inside_shutdown, "work ran inline during pre_exit");
    trace.push_back(value);
}

/// Checks the literal sequence of observable callbacks.
/// - Complexity: linear in the sequence length.
void expect(std::initializer_list<int> values) {
    require(trace == std::vector<int>(values), "shutdown callback order changed");
}

/// Initiates shutdown without running deferred work on the caller's stack.
void retire() {
    inside_shutdown = true;
    stlab::pre_exit();
    inside_shutdown = false;
    shutdown_returned = true;
}

/// Terminates after the current noexcept callback returns.
void finish() { emscripten_async_call(&main_executor_test::force_exit_success, nullptr, 0); }

/// Runs last among the handlers, before producer main work and the exit fence.
void oldest_handler() noexcept {
    expect({0, 1, 2, 3, 4, 5, 6, 7, 8});
    record(9);
    stlab::main_executor([]() noexcept { record(20); });
}

/// Runs a handler registered by another handler during deferred unwind.
void nested_handler() noexcept { record(8); }

/// Verifies that core drain precedes older handlers and preserves LIFO registration.
void after_drain() noexcept {
    require(shutdown_returned, "older handler ran before shutdown returned");
    expect({0, 1, 2, 3, 4, 5, 6});
    record(7);
    stlab::at_pre_exit(&nested_handler);
}

/// Runs before the lazily registered core handler.
void before_drain() noexcept { trace.push_back(0); }

/// Verifies that the initiating executor task and its capture cleanup have completed.
void after_current_task() noexcept {
    require(shutdown_returned && capture_destroyed, "drain skipped the initiating capture");
    expect({1, 2, 3, 4});
    record(5);
}

/// Verifies that even an empty initialized executor defers older handlers.
void after_empty_drain() noexcept {
    require(shutdown_returned, "empty drain ran older handlers inline");
    record(1);
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view scenario = argc > 1 ? argv[1] : "pending";
    if (scenario == "late_default" || scenario == "late_high" || scenario == "late_low" ||
        scenario == "shutdown_first" || scenario == "repeat_pre_exit") {
        std::set_terminate([] {
            std::fputs("EXPECTED_STLAB_TERMINATE\n", stderr);
            std::abort();
        });
        main_executor_test::run(argc, argv, [scenario] {
            stlab::main_executor([scenario]() noexcept {
                if (scenario != "shutdown_first") stlab::default_executor([]() noexcept {});
                retire();
                if (scenario == "repeat_pre_exit") {
                    stlab::pre_exit();
                    finish();
                    return;
                }
                stlab::main_executor([scenario]() noexcept {
                    if (scenario == "late_high")
                        stlab::high_executor([]() noexcept {});
                    else if (scenario == "late_low")
                        stlab::low_executor([]() noexcept {});
                    else
                        stlab::default_executor([]() noexcept {});
                    finish();
                });
            });
        });
    }
    main_executor_test::run(argc, argv, [scenario] {
        if (scenario == "normal_fifo") {
            stlab::default_executor([]() noexcept { record(1); });
            stlab::main_executor([]() noexcept { record(2); });
            stlab::high_executor([]() noexcept { record(3); });
            stlab::low_executor([]() noexcept { record(4); });
            stlab::main_executor([]() noexcept {
                expect({1, 2, 3, 4});
                retire();
                stlab::main_executor([]() noexcept { finish(); });
            });
        } else if (scenario == "future_chain") {
            auto result = stlab::async(stlab::default_executor, [] {
                              return 41;
                          }).then(stlab::default_executor, [](int value) { return value + 1; });
            retire();
            stlab::main_executor([result = std::move(result)]() mutable noexcept {
                require(result.is_ready(), "exit fence overtook a future continuation");
                require(stlab::await(std::move(result)) == 42, "drain lost the future result");
                finish();
            });
        } else if (scenario == "outside") {
            stlab::at_pre_exit(&after_empty_drain);
            stlab::default_executor([]() noexcept {
                record(2);
                stlab::main_executor([]() noexcept { record(3); });
            });
            retire();
            expect({});
            stlab::main_executor([]() noexcept {
                expect({2, 1, 3});
                finish();
            });
        } else if (scenario == "timer_only") {
            stlab::at_pre_exit(&after_empty_drain);
            auto capture = std::make_shared<int>(42);
            const std::weak_ptr<int> pending = capture;
            stlab::system_timer(std::chrono::hours(1440),
                                [capture = std::move(capture)]() noexcept {
                                    require(false, "canceled timer ran");
                                });
            retire();
            require(pending.expired(), "timer captures survived shutdown initiation");
            expect({});
            stlab::main_executor([]() noexcept {
                expect({1});
                finish();
            });
        } else if (scenario == "empty") {
            stlab::at_pre_exit(&after_empty_drain);
            stlab::default_executor([]() noexcept {});
            stlab::main_executor([]() noexcept {
                retire();
                expect({});
                stlab::main_executor([]() noexcept {
                    expect({1});
                    finish();
                });
            });
        } else if (scenario == "current") {
            stlab::at_pre_exit(&after_current_task);
            auto capture = std::shared_ptr<int>(new int(42), [](int* value) noexcept {
                delete value;
                capture_destroyed = true;
                stlab::default_executor([]() noexcept {
                    record(4);
                    stlab::main_executor([]() noexcept { record(7); });
                });
            });
            stlab::default_executor([capture = std::move(capture)]() noexcept {
                record(1);
                retire();
                stlab::high_executor([]() noexcept {
                    record(2);
                    stlab::main_executor([]() noexcept { record(6); });
                });
                stlab::low_executor([]() noexcept { record(3); });
                stlab::main_executor([]() noexcept {
                    expect({1, 2, 3, 4, 5, 6, 7});
                    finish();
                });
            });
        } else {
            require(scenario == "pending", "unknown shutdown scenario");
            stlab::at_pre_exit(&oldest_handler);
            stlab::at_pre_exit(&after_drain);
            stlab::main_executor([]() noexcept {
                stlab::main_executor([]() noexcept { record(10); });
                stlab::default_executor([]() noexcept {
                    record(1);
                    stlab::high_executor([]() noexcept { record(5); });
                    stlab::main_executor([]() noexcept { record(11); });
                });
                stlab::high_executor([]() noexcept { record(2); });
                stlab::low_executor([]() noexcept { record(3); });
                stlab::at_pre_exit(&before_drain);
                retire();
                expect({0});
                stlab::high_executor([]() noexcept {
                    record(4);
                    stlab::low_executor([]() noexcept { record(6); });
                    stlab::main_executor([]() noexcept { record(12); });
                });
                stlab::main_executor([]() noexcept {
                    expect({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 20});
                    finish();
                });
            });
        }
    });
}
