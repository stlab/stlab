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

#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>

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

/// Vtable describing how to relocate, invoke, and destroy a task's target, shared across the ABI
/// so submission does not depend on the target's concrete type.
///
/// - Note: identical in layout to `task<void() noexcept>::concept_t`.
using stlab_v2_task_concept_t = task<void() noexcept>::concept_t;

/// Submits one task to the shared default-priority executor.
///
/// - Precondition: `vtable` and `invoke` are not `nullptr`.
/// - Precondition: `source` is the `relocation_source()` of a live `task<void() noexcept>` sharing
///   `vtable`/`invoke`, valid for the duration of this call.
/// - Postcondition: exactly one invocation of the relocated target is scheduled.
extern "C" void stlab_v2_default_executor_submit(const stlab_v2_task_concept_t* vtable,
                                                 stlab_v2_task_proc invoke,
                                                 void* source) noexcept;

/// Submits one task to the shared high-priority executor.
///
/// - Precondition: `vtable` and `invoke` are not `nullptr`.
/// - Precondition: `source` is the `relocation_source()` of a live `task<void() noexcept>` sharing
///   `vtable`/`invoke`, valid for the duration of this call.
/// - Postcondition: exactly one invocation of the relocated target is scheduled.
extern "C" void stlab_v2_high_executor_submit(const stlab_v2_task_concept_t* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept;

/// Submits one task to the shared low-priority executor.
///
/// - Precondition: `vtable` and `invoke` are not `nullptr`.
/// - Precondition: `source` is the `relocation_source()` of a live `task<void() noexcept>` sharing
///   `vtable`/`invoke`, valid for the duration of this call.
/// - Postcondition: exactly one invocation of the relocated target is scheduled.
extern "C" void stlab_v2_low_executor_submit(const stlab_v2_task_concept_t* vtable,
                                             stlab_v2_task_proc invoke,
                                             void* source) noexcept;

/// Notifies the shared default executor that the calling thread is about to wait.
///
/// The portable task system may add a worker to preserve forward progress. Task systems with
/// operating-system-managed blocking compensation perform no action.
extern "C" void stlab_v2_notify_default_executor_before_waiting() noexcept;

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

/// Submits one task to the executor for `priority` by relocating its target across the ABI.
///
/// - Precondition: `t` holds a live target (`t != nullptr`).
/// - Postcondition: exactly one invocation of `t`'s target is scheduled; `t`'s target is left
///   moved-from (the caller must still let `t` be destroyed normally).
inline void submit_executor_proc(executor_priority priority, task<void() noexcept>& t) {
    const auto* vtable = t.relocation_concept();
    const auto invoke = t.relocation_invoke();
    auto* source = t.relocation_source();
    switch (priority) {
        case executor_priority::high:
            stlab_v2_high_executor_submit(vtable, invoke, source);
            break;
        case executor_priority::medium:
            stlab_v2_default_executor_submit(vtable, invoke, source);
            break;
        case executor_priority::low:
            stlab_v2_low_executor_submit(vtable, invoke, source);
            break;
    }
}

/**************************************************************************************************/

template <executor_priority P = executor_priority::medium>
struct executor_type {
    using result_type = void;

    template <class F>
    auto operator()(F&& f) const -> std::enable_if_t<std::is_nothrow_invocable_v<std::decay_t<F>>> {
        task<void() noexcept> t{std::forward<F>(f)};
        submit_executor_proc(P, t);
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
