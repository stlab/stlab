/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract (portable backend): after `pre_exit()`, pending and later-submitted main-executor
// tasks are destroyed without being invoked.

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/pre_exit.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

namespace {

std::weak_ptr<int> pending_state;
bool pending_invoked = false;
bool later_invoked = false;

void report(bool ok, const char* message) {
    if (!ok) {
        (void)std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main(int argc, char** argv) {
    main_executor_test::run(argc, argv, [] {
        stlab::main_executor([]() noexcept {
            stlab::pre_exit();
            report(pending_state.expired(), "pending task was not destroyed by pre_exit()");
            report(!pending_invoked, "pending task was invoked");

            auto later = std::make_shared<int>(0);
            std::weak_ptr<int> later_state = later;
            stlab::main_executor([p = std::move(later)]() noexcept { later_invoked = true; });
            report(later_state.expired(), "task submitted after pre_exit() was not destroyed");
            report(!later_invoked, "task submitted after pre_exit() was invoked");

            std::exit(EXIT_SUCCESS); // pre_exit() already ran.
        });

        auto pending = std::make_shared<int>(0);
        pending_state = pending;
        stlab::main_executor([p = std::move(pending)]() noexcept { pending_invoked = true; });
    });
}
