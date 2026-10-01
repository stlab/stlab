/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/main_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/config.hpp>
#include <stlab/pre_exit.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>

#if STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
#include <emscripten.h>
#endif

namespace {

std::promise<void> stop_source;
std::shared_future<void> stop = stop_source.get_future().share();
int completed = 0;
bool inside_shutdown = false;

/// Stops the scenario when the shutdown fence overtakes queued producer work.
void require(bool value, const char* message) {
    if (!value) {
        (void)std::fprintf(stderr, "FAILED: %s\n", message);
        std::abort();
    }
}

/// Releases producers from the application handler registered after core first-use.
void release_producers() noexcept { stop_source.set_value(); }

/// Counts producer work serviced by the main queue outside synchronous shutdown.
void complete() noexcept {
    require(!inside_shutdown, "main work ran inline during pre_exit()");
    ++completed;
}

/// Retires producers and posts the final exit task behind their main-queue work.
void shutdown() noexcept {
    inside_shutdown = true;
    stlab::pre_exit();
    inside_shutdown = false;
    stlab::main_executor([]() noexcept {
        require(completed == 101, "exit overtook producer main-queue work");
#if STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
        emscripten_async_call(&main_executor_test::force_exit_success, nullptr, 0);
#else
        std::exit(EXIT_SUCCESS);
#endif
    });
}

} // namespace

int main(int argc, char** argv) {
    main_executor_test::run(argc, argv, [] {
        stlab::default_executor([]() noexcept {
#if !STLAB_TASK_SYSTEM(EMSCRIPTEN)
            stop.wait();
#endif
            for (int i = 0; i != 100; ++i)
                stlab::main_executor(&complete);
        });
        stlab::at_pre_exit(&release_producers);
        stlab::system_timer(std::chrono::milliseconds(0), []() noexcept {
            stlab::main_executor(&shutdown);
#if !STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
            stop.wait();
#endif
            stlab::main_executor(&complete);
        });
    });
}
