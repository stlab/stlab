/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_DEFAULT_EXECUTOR_HPP
#define STLAB_CONCURRENCY_DEFAULT_EXECUTOR_HPP

/*! @file default_executor.hpp
 *  @brief Thread-pool executors mapping to the OS scheduler (libdispatch, Windows pool, portable).
 *
 *  @details
 *  Maps to the OS thread pool when the platform provides one; otherwise uses the library's
 *  portable implementation. Common configurations include Apple (Grand Central Dispatch),
 *  Windows thread pools, and Emscripten/WebAssembly builds per `STLAB_TASK_SYSTEM`.
 *
 *  Submit work through `high_executor`, `default_executor`, or `low_executor` as **priority
 *  hints**; the runtime prefers high, then default, then low, but order is not strict under load.
 *
 *  @note Call `pre_exit()` before normal process exit when using these executors so detached tasks
 *  do not overlap teardown of globals or other exit handlers (the implementation registers a
 *  pre-exit hook). `std::quick_exit()` is an alternative when it fits your program.
 */

#include <stlab/config.hpp>

#include <cassert>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include <stlab/concurrency/task.hpp>

#if STLAB_TASK_SYSTEM(LIBDISPATCH)
#include <dispatch/dispatch.h>
#endif

/**************************************************************************************************/

namespace stlab {
inline namespace v2 {

/** @defgroup stlab_concurrency_executor_abi executor_abi
 *  @ingroup stlab_concurrency_default_executor
 *  @brief ABI-stable task submission entry points for the shared executor core.
 *  @{
 */

/// Function pointer type submitted through the shared executor ABI.
///
/// - Precondition: `task` does not throw.
using stlab_v2_task_proc = void (*)(void*) noexcept;

/// Submits one task to the shared default-priority executor.
///
/// - Precondition: `task` is not `nullptr`.
/// - Precondition: `context` remains valid until `task(context)` is invoked.
/// - Postcondition: exactly one invocation of `task(context)` is scheduled.
extern "C" void stlab_v2_default_executor_submit(stlab_v2_task_proc task, void* context);

/// Submits one task to the shared high-priority executor.
///
/// - Precondition: `task` is not `nullptr`.
/// - Precondition: `context` remains valid until `task(context)` is invoked.
/// - Postcondition: exactly one invocation of `task(context)` is scheduled.
extern "C" void stlab_v2_high_executor_submit(stlab_v2_task_proc task, void* context);

/// Submits one task to the shared low-priority executor.
///
/// - Precondition: `task` is not `nullptr`.
/// - Precondition: `context` remains valid until `task(context)` is invoked.
/// - Postcondition: exactly one invocation of `task(context)` is scheduled.
extern "C" void stlab_v2_low_executor_submit(stlab_v2_task_proc task, void* context);

/** @} */

} // namespace v2

STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_default_executor default_executor
 *  @ingroup stlab_concurrency
 *  @brief Default thread-pool style executors (platform task system).
 *  @{
 */

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

enum class executor_priority : std::uint8_t { high, medium, low };

/// Submits one task to the shared executor implementation for `priority`.
///
/// - Precondition: `f` does not throw.
/// - Postcondition: exactly one execution of `f` is scheduled.
void submit_executor_task(executor_priority priority, task<void() noexcept>&& f);

/**************************************************************************************************/

#if STLAB_TASK_SYSTEM(LIBDISPATCH)

constexpr auto platform_priority(executor_priority p) {
    switch (p) {
        case executor_priority::high:
            return DISPATCH_QUEUE_PRIORITY_HIGH;
        case executor_priority::medium:
            return DISPATCH_QUEUE_PRIORITY_DEFAULT;
        case executor_priority::low:
            return DISPATCH_QUEUE_PRIORITY_LOW;
        default:
            assert(false && "Unknown value!");
    }
    return DISPATCH_QUEUE_PRIORITY_DEFAULT;
}

struct group_t {
    dispatch_group_t _group = dispatch_group_create();
    group_t() = default;
    group_t(const group_t&) = delete;
    group_t(group_t&& a) noexcept : _group(std::exchange(a._group, nullptr)) {}
    auto operator=(const group_t&) -> group_t& = delete;
    auto operator=(group_t&& a) noexcept -> group_t& {
        _group = std::exchange(a._group, nullptr);
        return *this;
    }

    ~group_t();
};

/// Returns the libdispatch group that tracks shared executor work.
auto group() -> const group_t&;

/**************************************************************************************************/

#elif STLAB_TASK_SYSTEM(PORTABLE)

class priority_task_system {
    struct implementation;
    std::unique_ptr<implementation> _impl;

public:
    priority_task_system();
    priority_task_system(const priority_task_system&) = delete;
    auto operator=(const priority_task_system&) -> priority_task_system& = delete;
    priority_task_system(priority_task_system&&) = delete;
    auto operator=(priority_task_system&&) -> priority_task_system& = delete;
    ~priority_task_system();

    /// Submits one task to the shared portable executor state.
    ///
    /// - Precondition: `f` does not throw.
    /// - Postcondition: exactly one execution of `f` is scheduled.
    void submit(executor_priority priority, task<void() noexcept>&& f);

    /// Wakes one waiting worker if one is available.
    auto wake() -> bool;

    /// Adds one expansion worker when the pool may otherwise stall.
    void add_thread();

    /// Joins all worker threads after shared executor shutdown begins.
    void join();
};

/// Returns the process-shared portable task system.
auto pts() -> priority_task_system&;

#endif

/**************************************************************************************************/

template <executor_priority P = executor_priority::medium>
struct executor_type {
    using result_type = void;

    template <class F>
    auto operator()(F&& f) const -> std::enable_if_t<std::is_nothrow_invocable_v<std::decay_t<F>>> {
        submit_executor_task(P, task<void() noexcept>{std::forward<F>(f)});
    }
};

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

/// Default task pool executor using low thread priority (when using the portable or Windows task
/// system).
inline constexpr auto low_executor = detail::executor_type<detail::executor_priority::low>{};
/// Default concurrent executor used by `stlab::async` and related APIs when none is specified.
inline constexpr auto default_executor = detail::executor_type<detail::executor_priority::medium>{};
/// Default task pool executor using high thread priority (when using the portable or Windows task
/// system).
inline constexpr auto high_executor = detail::executor_type<detail::executor_priority::high>{};

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
