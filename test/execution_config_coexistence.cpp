#if TEST_STLAB_FIRST
#include <stlab/config.hpp>
#include <stlab/execution/config.hpp>
#else
#include <stlab/execution/config.hpp>
#ifdef STLAB_STD_COROUTINES
#error "Execution must not define STLab's coroutine policy"
#endif
#include <stlab/config.hpp>
#endif

#ifndef STLAB_STD_COROUTINES
#error STLab must define its coroutine policy
#endif
static_assert(STLAB_STD_COROUTINES() == TEST_EXPECT_COROUTINES);

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/main_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/pre_exit.hpp>

/// Instantiates the future API independently of execution's build standard.
stlab::future<int> config_future() {
#if STLAB_STD_COROUTINES()
    co_return 42;
#else
    auto [task, future] = stlab::package<int()>(stlab::immediate_executor, [] { return 42; });
    task();
    return future;
#endif
}
