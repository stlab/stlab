/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/main_task_queue.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>

#include <emscripten.h>

#if defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten/threading.h>
#endif

#include <cassert>
#include <cstdlib>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

/// Runs the oldest queued task. Each wake is posted for exactly one pushed task.
void run_one(void* /*context*/) noexcept { main_tasks().pop()(); }

#if defined(__EMSCRIPTEN_PTHREADS__)
/// Defers `run_one` to the main runtime thread's event loop.
///
/// `emscripten_async_run_in_main_runtime_thread()` may run its function at any POSIX thread
/// cancellation point while wasm is executing on the main thread, which can re-enter code holding
/// locks. Bouncing through `emscripten_async_call()` runs the task from the main run loop instead.
void bounce(void* context) noexcept { emscripten_async_call(&run_one, context, 0); }
#endif

} // namespace
} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Submits one task to the Emscripten main runtime thread.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    STLAB_VERSION_NAMESPACE()::detail::main_tasks().push(vtable, invoke, source);
#if defined(__EMSCRIPTEN_PTHREADS__)
    emscripten_async_run_in_main_runtime_thread(
        EM_FUNC_SIG_VI, &STLAB_VERSION_NAMESPACE()::detail::bounce, nullptr);
#else
    emscripten_async_call(&STLAB_VERSION_NAMESPACE()::detail::run_one, nullptr, 0);
#endif
}

/// Ends the calling thread while keeping the runtime alive to service the main queue; never
/// returns.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() {
    emscripten_exit_with_live_runtime();
    std::abort();
}

} // namespace v2
} // namespace stlab
