/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef STLAB_TEST_MAIN_EXECUTOR_TEST_HOST_HPP
#define STLAB_TEST_MAIN_EXECUTOR_TEST_HOST_HPP

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>
#include <stlab/pre_exit.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#if STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)
#include <QCoreApplication>
#endif
#if STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
#include <emscripten.h>
#endif

namespace main_executor_test {

/// Returns the shared run-started marker.
inline auto run_started_state() noexcept -> std::atomic<bool>& {
    static std::atomic<bool> started{false};
    return started;
}

/// Returns whether the host has started servicing the main queue.
inline auto run_started() noexcept -> bool {
    return run_started_state().load(std::memory_order_acquire);
}

/// Marks the main queue as started.
inline void mark_run_started() noexcept {
    run_started_state().store(true, std::memory_order_release);
}

#if STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
/// Terminates the Emscripten runtime after the `noexcept` main-executor task returns.
inline void force_exit_success(void*) { emscripten_force_exit(EXIT_SUCCESS); }

/// Terminates the Emscripten runtime after the `noexcept` main-executor task returns.
inline void force_exit_failure(void*) { emscripten_force_exit(EXIT_FAILURE); }
#endif

/// Runs pre-exit handlers and terminates the process with a status reflecting `ok`.
///
/// - Postcondition: never returns, except on Emscripten where runtime shutdown is scheduled in a
///   later callback so the current `noexcept` task can return; prints `message` to `stderr` when
///   `ok` is `false`.
#if !STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
[[noreturn]]
#endif
inline void finish(bool ok, const char* message) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", message);
    stlab::pre_exit();
#if STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
    emscripten_async_call(ok ? &force_exit_success : &force_exit_failure, nullptr, 0);
#else
    std::exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
#endif
}

/// Fails the process if the main queue has not started.
inline void require_run_started() {
    if (!run_started()) finish(false, "task ran before main_executor_run() started");
}

/// Establishes the host application the backend requires, calls `start()`, then services the main
/// queue on the calling thread.
///
/// - Precondition: called once, from `main()`, with `main()`'s own `argc` (Qt retains a
///   reference to it).
template <class F>
[[noreturn]] void run(int& argc, char** argv, F start) {
#if STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)
    QCoreApplication application{argc, argv}; // Never destroyed: run() does not return.
#else
    (void)argc;
    (void)argv;
#endif
    start();
    mark_run_started();
    stlab::main_executor_run();
}

} // namespace main_executor_test

#endif
