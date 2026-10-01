/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/main_task_queue.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>

#include <dispatch/dispatch.h>

#include <cassert>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

/// Runs the oldest queued task. Each wake is posted for exactly one pushed task.
void run_one(void* /*context*/) noexcept { main_tasks().pop()(); }

} // namespace
} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Submits one task to the libdispatch main queue.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    STLAB_VERSION_NAMESPACE()::detail::main_tasks().push(vtable, invoke, source);
    dispatch_async_f(dispatch_get_main_queue(), nullptr,
                     &STLAB_VERSION_NAMESPACE()::detail::run_one);
}

/// Services the libdispatch main queue; never returns.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() { dispatch_main(); }

} // namespace v2
} // namespace stlab
