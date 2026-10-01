/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract: the main executor remains available after shutdown before its first submission.

#include "main_executor_test_host.hpp"
#include <stlab/concurrency/main_executor.hpp>
#include <stlab/pre_exit.hpp>

#include <cstdio>
#include <cstdlib>

namespace {

/// Observes when a task capture is destroyed.
struct destruction_probe {
    bool* _destroyed;

    /// Tracks destruction through `destroyed`.
    explicit destruction_probe(bool& destroyed) noexcept : _destroyed{&destroyed} {}

    /// Transfers the tracked flag.
    destruction_probe(destruction_probe&& x) noexcept : _destroyed{x._destroyed} {
        x._destroyed = nullptr;
    }

    /// Copying is disabled so exactly one active capture owns the flag.
    destruction_probe(const destruction_probe&) = delete;

    /// Copy assignment is disabled so exactly one active capture owns the flag.
    auto operator=(const destruction_probe&) -> destruction_probe& = delete;

    /// Move assignment is disabled because captures are only constructed and moved into tasks.
    auto operator=(destruction_probe&&) -> destruction_probe& = delete;

    /// Marks the tracked flag when this is the active capture.
    ~destruction_probe() {
        if (_destroyed != nullptr) *_destroyed = true;
    }
};

/// Terminates the process with failure when `ok` is `false`.
void report(bool ok, const char* message) {
    if (!ok) {
        (void)std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main(int argc, char** argv) {
    stlab::pre_exit();

    bool invoked = false;
    bool destroyed = false;
    stlab::main_executor([probe = destruction_probe{destroyed}, &invoked]() noexcept {
        (void)probe;
        invoked = true;
    });

    report(!destroyed, "first main submission after pre_exit() was discarded");
    report(!invoked, "first main submission after pre_exit() ran inline");
    main_executor_test::run(argc, argv, [&] {
        stlab::main_executor([&]() noexcept {
            report(invoked && destroyed, "main task did not execute and release its capture");
            std::exit(EXIT_SUCCESS);
        });
    });
}
