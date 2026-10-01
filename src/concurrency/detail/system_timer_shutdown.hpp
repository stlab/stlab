/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef STLAB_SRC_CONCURRENCY_DETAIL_SYSTEM_TIMER_SHUTDOWN_HPP
#define STLAB_SRC_CONCURRENCY_DETAIL_SYSTEM_TIMER_SHUTDOWN_HPP

#include <stlab/config.hpp>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

/// Closes timer admission, releases pending captures, and synchronizes committed callbacks before
/// default executors are joined within the shared core cleanup handler.
///
/// - Precondition: on native backends, not called from a timer callback.
/// - Precondition: if shutdown blocks main-queue servicing, committed callbacks do not
///   synchronously require main-queue progress.
/// - Complexity: linear in the number of pending timers.
void shutdown_system_timer() noexcept;

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

#endif
