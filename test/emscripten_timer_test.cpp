/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/pre_exit.hpp>

#include <emscripten.h>
#include <emscripten/threading.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string_view>
#include <utility>

#if defined(__EMSCRIPTEN_PTHREADS__)
#include <thread>
#endif

namespace {

std::atomic<int> completed{0};
std::chrono::steady_clock::time_point deadline;
bool submitting = true;
std::weak_ptr<int> pending_capture;

/// Stops a scenario when timer placement or timing violates its contract.
void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::abort();
    }
}

/// Checks thread placement and counts a timer completion.
void complete() noexcept {
    require(emscripten_is_main_runtime_thread(), "timer ran off the main runtime thread");
    ++completed;
}

/// Waits cooperatively for accepted callbacks, then cancels the far-future task.
void finish_when_ready() noexcept {
    if (completed != 4) {
        stlab::system_timer(std::chrono::milliseconds(1), &finish_when_ready);
        return;
    }
    stlab::pre_exit();
    require(pending_capture.expired(), "shutdown retained canceled capture");
    emscripten_async_call(&main_executor_test::force_exit_success, nullptr, 0);
}

} // namespace

int main(int argc, char** argv) {
    using namespace std::chrono_literals;
    if (argc > 1) {
        const std::string_view scenario{argv[1]};
        if (scenario == "cancel_capture_reentry") {
            std::set_terminate([] {
                (void)std::fputs("EXPECTED_STLAB_TERMINATE\n", stderr);
                std::abort();
            });
            main_executor_test::run(argc, argv, [] {
                stlab::main_executor([]() noexcept {
                    auto capture = std::shared_ptr<int>(new int(42), [](int* value) noexcept {
                        delete value;
                        stlab::system_timer(0ns, []() noexcept { std::abort(); });
                    });
                    stlab::system_timer(1440h, [capture = std::move(capture)]() noexcept {
                        require(false, "canceled timer executed");
                    });
                    stlab::pre_exit();
                    emscripten_async_call(&main_executor_test::force_exit_success, nullptr, 0);
                });
            });
        }
        if (scenario == "abi_guard" || scenario == "negative_abi_delay") {
            std::set_terminate([] {
                (void)std::fputs("EXPECTED_STLAB_TERMINATE\n", stderr);
                std::abort();
            });
            static const unsigned char incompatible_guard = 0;
            stlab::task<void() noexcept> target = []() noexcept { std::abort(); };
            const auto* guard = scenario == "abi_guard" ?
                                    &incompatible_guard :
                                    &stlab::detail::current_task_storage_abi_guard::value;
            (void)stlab::stlab_v2_system_timer_submit(
                guard, target.relocation_concept(), target.relocation_invoke(),
                target.relocation_source(), scenario == "abi_guard" ? 1000000000 : -7);
            stlab::pre_exit();
            emscripten_force_exit(EXIT_SUCCESS);
        }
        main_executor_test::run(argc, argv, [scenario] {
            stlab::main_executor([scenario]() noexcept {
                auto submit_pending = [] {
                    auto capture = std::make_shared<int>(42);
                    pending_capture = capture;
                    stlab::system_timer(1440h, [capture = std::move(capture)]() noexcept {
                        require(false, "canceled proxy registration executed");
                    });
                };
#if defined(__EMSCRIPTEN_PTHREADS__)
                if (scenario == "shutdown_worker") {
                    std::thread([submit_pending] {
                        submit_pending();
                        stlab::pre_exit();
                        require(pending_capture.expired(), "worker shutdown retained capture");
                        stlab::main_executor([]() noexcept {
                            emscripten_async_call(&main_executor_test::force_exit_success, nullptr,
                                                  0);
                        });
                    }).detach();
                    return;
                }
                std::thread worker{submit_pending};
                worker.join();
#else
                (void)scenario;
                submit_pending();
#endif
                stlab::pre_exit();
                require(pending_capture.expired(), "shutdown retained queued registration");
                emscripten_async_call(&main_executor_test::force_exit_success, nullptr, 0);
            });
        });
    }
    main_executor_test::run(argc, argv, [] {
        stlab::main_executor([]() noexcept {
            auto capture = std::make_shared<int>(42);
            pending_capture = capture;
            stlab::system_timer(1440h, [capture = std::move(capture)]() noexcept {
                require(false, "canceled timer ran");
            });
            deadline = std::chrono::steady_clock::now() + 20ms;
            stlab::system_timer(deadline, []() noexcept {
                require(std::chrono::steady_clock::now() >= deadline, "deadline timer fired early");
                complete();
            });
            stlab::system_timer(0ns, []() noexcept {
                require(!submitting, "timer executed inline");
                complete();
            });
            stlab::system_timer(std::chrono::steady_clock::time_point::min(), &complete);
#if defined(__EMSCRIPTEN_PTHREADS__)
            std::thread worker([] { stlab::system_timer(1ms, &complete); });
            worker.join();
#else
        stlab::system_timer(-1ns, &complete);
#endif
            submitting = false;
            stlab::system_timer(1ms, &finish_when_ready);
        });
    });
}
