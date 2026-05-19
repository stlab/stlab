/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_EXECUTOR_BASE_HPP
#define STLAB_CONCURRENCY_EXECUTOR_BASE_HPP

/*! @file executor_base.hpp
 *  @brief Executor type aliases and scheduling helpers.
 *
 *  @details
 *  `executor_t` is `std::function<void(task<void() noexcept>&&)>`. `execute_at` and
 *  `execute_delayed` wrap an executor to post work after a duration (or deprecated time point).
 *  `operator&` combines an `executor` with a callable into `executor_task_pair` for `future::then`
 *  and channel attachment.
 */

#include <stlab/config.hpp>

#include <chrono>
#include <functional>

#include <stlab/concurrency/system_timer.hpp>
#include <stlab/concurrency/task.hpp>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_executor_base executor_base
 *  @ingroup stlab_concurrency
 *  @brief Executor type aliases and scheduling helpers.
 *  @{
 */

/**************************************************************************************************/

/// Type-erased executor: accepts a `void() noexcept` task.
using executor_t = std::function<void(stlab::task<void() noexcept>)>;

/// Returns an executor that posts tasks to `executor` after `duration` (immediate if zero).
template <typename Rep, typename Per = std::ratio<1>>
auto execute_at(std::chrono::duration<Rep, Per> duration, executor_t executor) -> executor_t {
    return [_duration = std::move(duration), _executor = std::move(executor)](auto f) mutable {
        if (_duration != std::chrono::duration<Rep, Per>{})
            system_timer(_duration,
                         [_f = std::move(f), _executor = std::move(_executor)]() mutable noexcept {
                             _executor(std::move(_f));
                         });
        else
            _executor(std::move(f));
    };
}

/// @deprecated Use `execute_at(duration, executor)` instead.
[[deprecated("Use chrono::duration as parameter instead")]] inline auto execute_at(
    std::chrono::steady_clock::time_point when, executor_t executor) -> executor_t {
    using namespace std::chrono;
    return execute_at(duration_cast<nanoseconds>(when - steady_clock::now()), std::move(executor));
}

/// Returns an executor that delays each submitted task by `duration` before forwarding to `executor`.
template <typename E, typename Rep, typename Per = std::ratio<1>>
auto execute_delayed(std::chrono::duration<Rep, Per> duration, E executor) {
    return execute_at(duration, std::move(executor));
}

/// Wraps an `executor_t` for use with `operator&`.
struct executor {
    executor_t _executor;
};

/// Executor plus callable, produced by `executor & f` (used by futures and channels).
template <typename F>
struct executor_task_pair {
    executor_t _executor;
    F _f;
};

template <typename F>
auto operator&(executor e, F&& f) -> executor_task_pair<F> {
    return executor_task_pair<F>{std::move(e._executor), std::forward<F>(f)};
}

template <typename F>
auto operator&(F&& f, executor e) -> executor_task_pair<F> {
    return executor_task_pair<F>{std::move(e._executor), std::forward<F>(f)};
}

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
