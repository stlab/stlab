/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_EXECUTOR_BASE_HPP
#define STLAB_CONCURRENCY_EXECUTOR_BASE_HPP

#include <stlab/config.hpp>

#include <chrono>
#include <functional>

#include <stlab/concurrency/system_timer.hpp>
#include <stlab/concurrency/task.hpp>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

using executor_t = std::function<void(stlab::task<void() noexcept>)>;

/*
 * returns an executor that will schedule any passed task to it to execute
 * at the given time point on the executor provided
 */

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

[[deprecated("Use chrono::duration as parameter instead")]] inline auto execute_at(
    std::chrono::steady_clock::time_point when, executor_t executor) -> executor_t {
    using namespace std::chrono;
    return execute_at(duration_cast<nanoseconds>(when - steady_clock::now()), std::move(executor));
}

/*
 * returns an executor that will schedule the task to execute on the provided
 * executor duration after it is invoked
 */

template <typename E, typename Rep, typename Per = std::ratio<1>>
auto execute_delayed(std::chrono::duration<Rep, Per> duration, E executor) {
    return execute_at(duration, std::move(executor));
}

struct executor {
    executor_t _executor;
};

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

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
