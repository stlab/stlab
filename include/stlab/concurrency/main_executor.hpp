/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_MAIN_EXECUTOR_HPP
#define STLAB_CONCURRENCY_MAIN_EXECUTOR_HPP

/*! @file main_executor.hpp
 *  @brief Executor for the application's main queue.
 *
 *  @details
 *  Tasks submitted to `main_executor` run in submission order on the main queue selected by
 *  `STLAB_MAIN_EXECUTOR` when `stlab-core` is built: the libdispatch main queue, the Qt
 *  application event loop, the Emscripten main runtime thread, or (opt-in) a portable
 *  stlab-owned queue. `main_executor_run()` services the main queue on the calling thread and
 *  never returns, like `dispatch_main()`. The main queue remains available after `pre_exit()`.
 *  A shutdown task can call `pre_exit()` to retire timer/default-executor producers, then post
 *  another main task that calls `std::exit()`. That exit task follows main work submitted by the
 *  retired producers, but does not drain work that earlier main tasks subsequently enqueue.
 *  Producers being joined must not synchronously depend on main-queue progress while the main
 *  thread is inside `pre_exit()`.
 *
 *  On Emscripten, the final main task schedules a separate `emscripten_async_call()` callback,
 *  returns normally, and calls `emscripten_force_exit()` from that callback; link the executable
 *  with `-sEXIT_RUNTIME=1` for it to terminate.
 *
 *  Windows has no process main queue (each UI thread owns its message queue), so no main executor
 *  is provided there unless `STLAB_MAIN_EXECUTOR` selects Qt or `portable`.
 */

#include <stlab/config.hpp>

#if !STLAB_MAIN_EXECUTOR(NONE)
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>

#include <type_traits>
#include <utility>
#endif

/**************************************************************************************************/

#if !STLAB_MAIN_EXECUTOR(NONE)

namespace stlab {
inline namespace v2 {

/** @addtogroup stlab_concurrency_executor_abi
 *  @{
 */

/// Submits one task to the main queue.
///
/// - Precondition: `task_abi_guard` points to `detail::current_task_storage_abi_guard::value`.
/// - Precondition: `vtable` and `invoke` are not `nullptr`.
/// - Precondition: `source` is the `relocation_source()` of a live `task<void() noexcept>` sharing
///   `vtable`/`invoke`, valid for the duration of this call.
/// - Postcondition: exactly one invocation of the relocated target is scheduled on the main queue,
///   after every task previously submitted from the calling thread.
/// - Postcondition: main submission remains available after `pre_exit()`.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* task_abi_guard,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept;

/// Services the main queue on the calling thread; never returns.
///
/// - Precondition: called at most once per process, from the thread the platform designates as
///   main where it designates one.
/// - Postcondition: on Emscripten, the runtime remains live until code calls
///   `emscripten_force_exit()` from outside a `noexcept` executor task, with the executable linked
///   with `-sEXIT_RUNTIME=1`.
extern "C" [[noreturn]] void stlab_v2_main_executor_run();

/** @} */

} // namespace v2
} // namespace stlab

#endif

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_main_executor main_executor
 *  @ingroup stlab_concurrency
 *  @brief Main-thread / UI-thread executor (Qt, libdispatch, Emscripten, etc.).
 *  @{
 */

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

#if !STLAB_MAIN_EXECUTOR(NONE)

/// Executor that submits `void() noexcept` tasks to the main queue through the shared core ABI.
struct main_executor_type {
    using result_type = void;

    /// Schedules `f` to run on the main queue after every task previously submitted from the
    /// calling thread.
    template <class F>
    auto operator()(F&& f) const -> std::enable_if_t<std::is_nothrow_invocable_v<std::decay_t<F>>> {
        task<void() noexcept> t{std::forward<F>(f)};
        stlab_v2_main_executor_submit(&current_task_storage_abi_guard::value,
                                      t.relocation_concept(), t.relocation_invoke(),
                                      t.relocation_source());
    }
};

/**************************************************************************************************/

#endif

} // namespace detail

#if !STLAB_MAIN_EXECUTOR(NONE)

/// Runs `void() noexcept` tasks in submission order on the configured main queue.
/// Remains available after `pre_exit()` so a final queued task can terminate the process.
inline constexpr auto main_executor = detail::main_executor_type{};

/// Services the main queue on the calling thread; never returns.
///
/// - Precondition: called at most once per process, from the thread the platform designates as
///   main where it designates one.
/// - Postcondition: on Emscripten, the runtime remains live until code calls
///   `emscripten_force_exit()` from outside a `noexcept` executor task.
[[noreturn]] inline void main_executor_run() { stlab_v2_main_executor_run(); }
#endif

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif // STLAB_CONCURRENCY_MAIN_EXECUTOR_HPP
