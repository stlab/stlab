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
#include <stlab/pre_exit.hpp>

#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace {

bool inside_shutdown = false;

/// Aborts when an observable shutdown guarantee is violated.
void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::abort();
    }
}

/// Initiates shutdown without running deferred work on the caller's stack.
void retire() {
    inside_shutdown = true;
    stlab::pre_exit();
    inside_shutdown = false;
}

/// Terminates after the current noexcept callback returns.
void finish() { emscripten_async_call(&main_executor_test::force_exit_success, nullptr, 0); }

} // namespace

/// Verifies that cooperative retirement drains STLab future continuations before the exit fence.
int main(int argc, char** argv) {
    const std::string_view scenario = argc > 1 ? argv[1] : "future_chain";
    require(scenario == "future_chain", "unknown shutdown scenario");
    main_executor_test::run(argc, argv, [] {
        auto result = stlab::async(stlab::default_executor, [] {
                          return 41;
                      }).then(stlab::default_executor, [](int value) { return value + 1; });
        retire();
        stlab::main_executor([result = std::move(result)]() mutable noexcept {
            require(!inside_shutdown, "exit fence ran inline during pre_exit");
            require(result.is_ready(), "exit fence overtook a future continuation");
            require(stlab::await(std::move(result)) == 42, "drain lost the future result");
            finish();
        });
    });
}
