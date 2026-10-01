/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract: pre_exit() retires core producers without closing the main queue.

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
            report(!pending_state.expired(), "pre_exit() discarded pending main work");
            report(!pending_invoked, "pending main work ran inside pre_exit()");

            auto later = std::make_shared<int>(0);
            std::weak_ptr<int> later_state = later;
            stlab::main_executor([p = std::move(later)]() noexcept { later_invoked = true; });
            report(!later_state.expired(), "post-shutdown main task was discarded");
            report(!later_invoked, "post-shutdown main task ran inline");
            stlab::main_executor([later_state]() noexcept {
                report(pending_invoked && later_invoked, "exit task overtook queued main work");
                report(pending_state.expired() && later_state.expired(),
                       "completed main tasks retained their captures");
                std::exit(EXIT_SUCCESS);
            });
        });

        auto pending = std::make_shared<int>(0);
        pending_state = pending;
        stlab::main_executor([p = std::move(pending)]() noexcept { pending_invoked = true; });
    });
}
