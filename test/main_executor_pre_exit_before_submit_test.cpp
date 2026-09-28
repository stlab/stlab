/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract (portable backend): after `pre_exit()`, later-submitted main-executor tasks are
// destroyed without being invoked, even when no task was submitted before `pre_exit()`.

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
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    stlab::pre_exit();

    bool invoked = false;
    bool destroyed = false;
    stlab::main_executor([probe = destruction_probe{destroyed}, &invoked]() noexcept {
        (void)probe;
        invoked = true;
    });

    report(destroyed, "task submitted after pre_exit() was not destroyed");
    report(!invoked, "task submitted after pre_exit() was invoked");

    std::exit(EXIT_SUCCESS); // pre_exit() already ran.
}
