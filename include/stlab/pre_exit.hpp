/*
    Copyright 2022 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

#ifndef STLAB_PRE_EXIT_HPP
#define STLAB_PRE_EXIT_HPP

/*! @file pre_exit.hpp
 *  @brief Register and run operations that must execute before program exit.
 *
 *  @details
 *  Register handlers with `at_pre_exit()`; call `pre_exit()` once before normal process termination
 *  (before `std::exit()` or when leaving `main()`). Handlers run in reverse registration order.
 *  Required when using the default executor so detached or canceled tasks do not overlap global
 *  teardown (see `default_executor.hpp`). `std::quick_exit()` is an alternative when appropriate.
 *
 *  Timer and default-executor resources share a teardown handler registered on their first use.
 *  Register application handlers that release running work after that first use and before calling
 *  `pre_exit()`, so they execute before core teardown. Core teardown cancels pending timers,
 *  waits for running timer callbacks,
 *  then drains the default executor.
 *
 *  On threadless Emscripten, `pre_exit()` initiates asynchronous retirement instead of blocking.
 *  Default/high/low work, including descendants and capture cleanup, drains through the event
 *  loop while ordinary main tasks are deferred. Earlier-registered handlers resume in LIFO
 *  order after that drain. A main task submitted after `pre_exit()` is the completion fence:
 *  producer main submissions and the remaining handlers precede it. Drain work must not depend
 *  on deferred main tasks for progress. Final process exit must follow that fence.
 */

/**************************************************************************************************/

// The namespace for pre_exit cannot be changed without an ABI break. If making an ABI breaking
// change in this file it needs to be done in a way supporting this version as well.

namespace stlab {
inline namespace v2 {

/** @defgroup stlab_pre_exit pre_exit
 *  @brief Pre-exit handler registration (`at_pre_exit`, `pre_exit`).
 *  @{
 */

/**************************************************************************************************/

/// Function type invoked during `pre_exit()` (must not throw; `noexcept` with C++17 and later).
using pre_exit_handler = void (*)() noexcept;

/// An `extern "C"` vector for `pre-exit()` to make it simpler to
/// export the function from a shared library.
extern "C" void stlab_pre_exit();
/// An `extern "C"` vector for `at_pre-exit()` to make it simpler to
/// export the function from a shared library.
extern "C" void stlab_at_pre_exit(pre_exit_handler f);

/// Invoke all registered pre-exit handlers in the reverse order they are registered. It is safe
/// to register additional handlers during this operation. Must be invoked exactly once prior to
/// program exit.
/// Handlers needed to unblock running core work must be registered after the first timer or
/// default-executor use and before invoking `pre_exit()`.
/// On threadless Emscripten, returns after initiating retirement; the next ordinary main task
/// runs only after executor work and deferred handlers finish. Default/high/low submission
/// remains available during the drain and is a precondition violation after it completes.
///
/// - Complexity: linear in invoked handlers, excluding the work performed by those handlers.
inline void pre_exit() { stlab_pre_exit(); }

/// Register a pre-exit handler. The `pre-exit-handler` may not throw. With C++17 or later it
/// is required to be `noexcept`.
inline void at_pre_exit(pre_exit_handler f) { stlab_at_pre_exit(f); }

/**************************************************************************************************/

/** @} */

} // namespace v2
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
