/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef STLAB_SRC_CONCURRENCY_DETAIL_CORE_SHUTDOWN_HPP
#define STLAB_SRC_CONCURRENCY_DETAIL_CORE_SHUTDOWN_HPP

#include <stlab/config.hpp>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

/// Cleanup operation for one initialized default-executor backend.
using core_executor_cleanup = void (*)() noexcept;

/// Pushes the shared core handler onto the public pre-exit stack through a throwing C++ entry.
///
/// - Precondition: `pre_exit()` has not completed.
/// - Throws: `std::bad_alloc` if handler registration cannot allocate.
void register_core_shutdown_handler(core_executor_cleanup cleanup);

/// Registers the single shared core handler at the first use of timers or default executors.
/// The handler retires timers and default executors without closing main admission. Cooperative
/// retirement defers ordinary main dispatch and the remaining handlers until executor quiescence.
/// When `pre_exit()` blocks the main thread, workers and timer callbacks must not synchronously
/// require main-queue progress.
///
/// - Precondition: core cleanup and `pre_exit()` have not completed.
/// - Throws: `std::bad_alloc` if handler registration cannot allocate; registration can be retried.
/// - Postcondition: later calls perform only atomic lifecycle checks.
void register_core_shutdown();

/// Registers one initialized default-executor backend for cleanup after timer callbacks finish.
///
/// - Precondition: called exactly once per backend, after `register_core_shutdown()`; at most
///   three backends are registered, and core cleanup has not completed.
/// - Postcondition: does not allocate; registration remains possible while timers are joining.
void register_core_executor_cleanup(core_executor_cleanup cleanup);

/// Closes core admission at executor retirement or stack exhaustion, including before first use.
void complete_core_shutdown() noexcept;

/// Suspends handler popping until cooperative executor retirement completes.
/// - Precondition: called by a handler during the active pre-exit operation.
void defer_pre_exit() noexcept;

/// Resumes deferred handler popping in registration-reverse order.
/// - Precondition: the pre-exit operation is deferred and core retirement has completed.
/// - Complexity: linear in invoked handlers, excluding their work.
void resume_pre_exit() noexcept;

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

#endif
