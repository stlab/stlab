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
 */

/**************************************************************************************************/

// The namespace for pre_exit cannot be changed without an ABI break. If making an ABI breaking
// change in this file it needs to be done in a way supporting this version as well.

namespace stlab {
inline namespace v2 {

/** @defgroup stlab_pre_exit pre_exit
 *  @ingroup stlab
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
