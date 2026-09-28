/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/main_task_queue.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>
#include <stlab/pre_exit.hpp>

#include <atomic>
#include <cassert>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

/// Returns the process-shared main queue, registering its pre-exit handler on first use.
///
/// The queue is intentionally never destroyed so no main-queue task is destroyed during static
/// destruction.
auto main_tasks() -> main_task_queue& {
    static auto& queue = *new main_task_queue; // NOLINT(cppcoreguidelines-owning-memory)
    static const bool registered = [] {
        at_pre_exit([]() noexcept { main_tasks().close(); });
        return true;
    }();
    (void)registered;
    return queue;
}

/// Set once the main queue starts being serviced.
std::atomic<bool> running{false};

} // namespace
} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Submits one task to the portable main queue.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    (void)STLAB_VERSION_NAMESPACE()::detail::main_tasks().push(vtable, invoke, source);
}

/// Services the portable main queue on the calling thread; never returns.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept {
    [[maybe_unused]] const bool was_running =
        STLAB_VERSION_NAMESPACE()::detail::running.exchange(true);
    assert(!was_running && "main_executor_run() called more than once.");
    auto& queue = STLAB_VERSION_NAMESPACE()::detail::main_tasks();
    while (true) {
        auto task = queue.wait_pop();
        task();
    }
}

} // namespace v2
} // namespace stlab
