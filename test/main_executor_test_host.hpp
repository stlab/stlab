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

#include <cstdio>
#include <cstdlib>

#if STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)
#include <QCoreApplication>
#endif

namespace main_executor_test {

/// Runs pre-exit handlers and terminates the process with a status reflecting `ok`.
///
/// - Postcondition: never returns; prints `message` to `stderr` when `ok` is `false`.
[[noreturn]] inline void finish(bool ok, const char* message) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", message);
    stlab::pre_exit();
    std::exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
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
    stlab::main_executor_run();
}

} // namespace main_executor_test

#endif
